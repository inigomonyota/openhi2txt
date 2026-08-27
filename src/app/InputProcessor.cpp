#include "app/InputProcessor.h"
#include "core/Processor.h"
#include "core/StructureSelector.h"
#include "core/Trace.h"
#include "io/ChdReader.h"
#include "io/Utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <limits>
#include <memory>
#include <unordered_map>

namespace fs = std::filesystem;

namespace openhi2txt {

namespace {

static void mergeRows(std::vector<std::unordered_map<std::string, Value>>& dst,
                      const std::vector<std::unordered_map<std::string, Value>>& src) {
    if (src.empty()) return;
    if (dst.size() < src.size()) dst.resize(src.size());

    for (size_t r = 0; r < src.size(); ++r) {
        for (const auto& kv : src[r]) {
            if (std::holds_alternative<std::monostate>(kv.second)) continue;

            auto existing = Utils::findIdentifier(dst[r], kv.first);
            if (existing == dst[r].end()) dst[r].emplace(kv.first, kv.second);
            else existing->second = kv.second;
        }
    }
}

enum class RawProcessStatus {
    NotMatched,
    Processed,
    Error
};

static std::string canonicalFileKind(const std::string& fileKind) {
    std::string kind = Utils::trim(fileKind.empty() ? ".hi" : fileKind);
    if (Utils::ieq(kind, "hi")) return ".hi";
    std::transform(kind.begin(), kind.end(), kind.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return kind;
}

static RawProcessStatus processRawStructure(InputProcessResult& res,
                                            const std::vector<uint8_t>& raw,
                                            const Structure& s,
                                            const GameDef& def,
                                            const std::string& sourceName,
                                            const TraceSink* trace) {
    const bool hasDefinitionChecks = !s.checkAll.empty() || !s.checkAny.empty();

    // Size is a strict selector only when definition bytes are absent.
    // This preserves official hi2txt's handling of layouts whose dumped
    // file contains bytes beyond the selected hiscore.dat regions.
    if (!hasDefinitionChecks && !s.checkSizes.empty()) {
        bool ok = false;
        for (int sz : s.checkSizes) {
            if (sz > 0 && static_cast<size_t>(sz) == raw.size()) {
                ok = true;
                if (trace) trace->line("TRACE: matching structure: size = " + std::to_string(sz));
                break;
            }
        }
        if (!ok) return RawProcessStatus::NotMatched;
    }
    else if (!s.checkSizes.empty()) {
        for (int sz : s.checkSizes) {
            if (sz > 0 && static_cast<size_t>(sz) == raw.size()) {
                if (trace) trace->line("TRACE: matching structure: size = " + std::to_string(sz));
                break;
            }
        }
    }

    if (hasDefinitionChecks) {
        bool ok = Processor::checkMatches(raw, s);
        if (!ok && s.byteSwap > 1) {
            auto swapped = StructureSelector::applyStructByteSwap(raw, s.byteSwap);
            ok = Processor::checkMatches(swapped, s);
        }
        if (!ok) return RawProcessStatus::NotMatched;
    }

    try {
        std::vector<uint8_t> bytes = StructureSelector::applyStructByteSwap(raw, s.byteSwap);
        bytes = StructureSelector::applyDecodeRegions(bytes, s.decodeRegions);
        if (trace) {
            for (const auto& region : s.decodeRegions) {
                trace->line("TRACE: decoded " + region.type + " region at offset " +
                    std::to_string(region.offset) + ", size " + std::to_string(region.size));
            }
        }
        res.ok = true;
        if (res.inputPath.empty()) {
            res.inputPath = fs::path(sourceName);
            res.outputId = s.outputId;
        }
        if (trace) trace->line("TRACE: data taken from source: " + sourceName);
        mergeRows(res.rows, Processor::extractRows(bytes, s, def, trace));
        return RawProcessStatus::Processed;
    }
    catch (const std::exception& e) {
        res.ok = false;
        res.errorKind = HiScoreErrorKind::InvalidData;
        res.error = e.what();
        return RawProcessStatus::Error;
    }
}

static std::vector<fs::path> gameChdCandidates(const fs::path& mameRoot,
                                                const std::string& requestedGame) {
    const fs::path roms = mameRoot / "roms";
    const fs::path gameDirectory = roms / requestedGame;
    const fs::path conventional = gameDirectory / (requestedGame + ".chd");
    std::vector<fs::path> candidates;
    if (fs::is_regular_file(conventional)) candidates.push_back(conventional);

    std::vector<fs::path> other;
    std::error_code ec;
    for (fs::directory_iterator it(gameDirectory, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) continue;
        const fs::path candidate = it->path();
        if (!Utils::ieq(candidate.extension().string(), ".chd") || candidate == conventional)
            continue;
        other.push_back(candidate);
    }
    std::sort(other.begin(), other.end());
    candidates.insert(candidates.end(), other.begin(), other.end());

    const fs::path flat = roms / (requestedGame + ".chd");
    if (fs::is_regular_file(flat)) candidates.push_back(flat);
    return candidates;
}

static fs::path resolveDifPath(const fs::path& mameRoot,
                               const std::string& requestedGame,
                               const fs::path& explicitInputPath) {
    if (!explicitInputPath.empty()) return explicitInputPath;

    const fs::path diffDirectory = mameRoot / "diff";
    const fs::path conventional = diffDirectory / (requestedGame + ".dif");
    if (fs::is_regular_file(conventional)) return conventional;

    for (const fs::path& chd : gameChdCandidates(mameRoot, requestedGame)) {
        const fs::path diskNamed = diffDirectory / (chd.stem().string() + ".dif");
        if (fs::is_regular_file(diskNamed)) return diskNamed;
    }
    return conventional;
}

static fs::path resolveInputPath(const fs::path& mameRoot,
                                 const std::string& requestedGame,
                                 const Structure& s,
                                 const fs::path& explicitInputPath = {}) {
    const std::string kind = Utils::trim(s.fileKind.empty() ? ".hi" : s.fileKind);

    // default: hiscore/<game>.hi
    if (kind == ".hi" || Utils::ieq(kind, "hi")) {
        if (!explicitInputPath.empty()) return explicitInputPath;
        return mameRoot / "hiscore" / (requestedGame + ".hi");
    }

    if (Utils::ieq(kind, "dif")) {
        return resolveDifPath(mameRoot, requestedGame, explicitInputPath);
    }

    // nvram: file="battery" etc => nvram/<game>/<file>
    if (!kind.empty() && kind[0] != '.') {
        return mameRoot / "nvram" / requestedGame / kind;
    }

    // fallback: treat as extension
    return mameRoot / "hiscore" / (requestedGame + kind);
}

static bool headerMatches(const fs::path& candidate,
                          const ChdHeaderInfo& overlay,
                          const TraceSink* trace) {
    ChdHeaderInfo parent;
    std::string ignoredError;
    if (!ChdReader::readHeader(candidate, parent, ignoredError)) return false;
    if (parent.sha1 != overlay.parentSha1) return false;
    if (trace) trace->line("TRACE: matching parent CHD: " + candidate.string());
    return true;
}

static fs::path findParentChd(const fs::path& mameRoot,
                              const std::string& requestedGame,
                              const ChdHeaderInfo& overlay,
                              const TraceSink* trace) {
    for (const fs::path& candidate : gameChdCandidates(mameRoot, requestedGame)) {
        if (headerMatches(candidate, overlay, trace)) return candidate;
    }
    return {};
}

} // namespace

InputProcessResult InputProcessor::process(const fs::path& mameRoot,
    const std::string& requestedGame,
    const GameDef& def,
    const fs::path& explicitInputPath,
    const TraceSink* trace) {
    InputProcessResult res;
    bool foundInputFile = false;
    fs::path openOverlayPath;
    std::unique_ptr<ChdReader> chdReader;

    if (trace) {
        for (const auto& s : def.structures) {
            fs::path p = resolveInputPath(mameRoot, requestedGame, s, explicitInputPath);
            trace->line("TRACE: potential file containing hiscore: " + p.string());
        }
    }

    for (const auto& s : def.structures) {
        fs::path p = resolveInputPath(mameRoot, requestedGame, s, explicitInputPath);

        std::vector<uint8_t> raw;
        if (Utils::ieq(Utils::trim(s.fileKind), "dif")) {
            if (!fs::is_regular_file(p)) continue;
            foundInputFile = true;

            if (!chdReader || p != openOverlayPath) {
                ChdHeaderInfo overlayHeader;
                std::string chdError;
                if (!ChdReader::readHeader(p, overlayHeader, chdError)) {
                    res.errorKind = HiScoreErrorKind::InvalidData;
                    res.error = chdError;
                    return res;
                }
                if (!overlayHeader.hasParent) {
                    res.errorKind = HiScoreErrorKind::InvalidData;
                    res.error = "The DIF does not identify a parent CHD: " + p.string();
                    return res;
                }

                const fs::path parent = findParentChd(
                    mameRoot, requestedGame, overlayHeader, trace);
                if (parent.empty()) {
                    res.errorKind = HiScoreErrorKind::InvalidData;
                    res.error = "No parent CHD matching the DIF was found under " +
                        (mameRoot / "roms" / requestedGame).string();
                    return res;
                }

                auto reader = std::make_unique<ChdReader>();
                if (!reader->open(parent, p, chdError)) {
                    res.errorKind = HiScoreErrorKind::InvalidData;
                    res.error = chdError;
                    return res;
                }
                chdReader = std::move(reader);
                openOverlayPath = p;
            }

            if (!s.hasInputWindow || s.inputLength > std::numeric_limits<size_t>::max()) {
                res.errorKind = HiScoreErrorKind::InvalidData;
                res.error = "The DIF structure has an invalid input window";
                return res;
            }
            std::string chdError;
            if (!chdReader->read(s.inputOffset, static_cast<size_t>(s.inputLength), raw, chdError)) {
                res.errorKind = HiScoreErrorKind::InvalidData;
                res.error = chdError;
                return res;
            }
            if (trace) {
                trace->line("TRACE: CHD logical byte window: offset=" +
                    std::to_string(s.inputOffset) + ", length=" +
                    std::to_string(s.inputLength));
            }
        }
        else {
            if (!Utils::readFileBytes(p, raw)) continue;
            foundInputFile = true;
        }

        const RawProcessStatus status = processRawStructure(
            res, raw, s, def, p.string(), trace);
        if (status == RawProcessStatus::Error) return res;
    }

    if (!res.ok) {
        res.errorKind = foundInputFile
            ? HiScoreErrorKind::StructureNotMatched
            : HiScoreErrorKind::InputNotFound;
        res.error = "No matching structure found under " + mameRoot.string();
    }
    return res;
}

InputProcessResult InputProcessor::processBuffers(const GameDef& def,
                                                  const std::vector<HiScoreInput>& inputs,
                                                  const TraceSink* trace) {
    InputProcessResult res;
    bool foundInput = false;

    for (const auto& s : def.structures) {
        const std::string structureKind = canonicalFileKind(s.fileKind);
        const auto found = std::find_if(inputs.begin(), inputs.end(),
            [&](const HiScoreInput& input) {
                return canonicalFileKind(input.fileKind) == structureKind;
            });
        if (found == inputs.end()) continue;
        foundInput = true;

        const std::string sourceName = found->sourceName.empty()
            ? "memory:" + structureKind
            : found->sourceName;
        const RawProcessStatus status = processRawStructure(
            res, found->bytes, s, def, sourceName, trace);
        if (status == RawProcessStatus::Error) return res;
    }

    if (!res.ok) {
        res.errorKind = foundInput
            ? HiScoreErrorKind::StructureNotMatched
            : HiScoreErrorKind::InputNotFound;
        res.error = "No matching structure found in provided input buffers";
    }
    return res;
}

} // namespace openhi2txt
