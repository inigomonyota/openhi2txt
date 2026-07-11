#include "app/InputProcessor.h"
#include "core/Processor.h"
#include "core/StructureSelector.h"
#include "core/Trace.h"
#include "io/Utils.h"

#include <filesystem>
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
            if (!std::holds_alternative<std::monostate>(kv.second))
                dst[r][kv.first] = kv.second;
        }
    }
}

static fs::path resolveInputPath(const fs::path& mameRoot,
                                 const std::string& requestedGame,
                                 const Structure& s,
                                 const fs::path& explicitInputPath = {}) {
    const std::string kind = Utils::trim(s.fileKind.empty() ? ".hi" : s.fileKind);

    // default: hiscore/<game>.hi
    if (kind == ".hi") {
        if (!explicitInputPath.empty()) return explicitInputPath;
        return mameRoot / "hiscore" / (requestedGame + ".hi");
    }

    // nvram: file="battery" etc => nvram/<game>/<file>
    if (!kind.empty() && kind[0] != '.') {
        return mameRoot / "nvram" / requestedGame / kind;
    }

    // fallback: treat as extension
    return mameRoot / "hiscore" / (requestedGame + kind);
}

static std::string normalizedFileKind(const Structure& s) {
    return Utils::trim(s.fileKind.empty() ? ".hi" : s.fileKind);
}

} // namespace

InputProcessResult InputProcessor::process(const fs::path& mameRoot,
    const std::string& requestedGame,
    const GameDef& def,
    const fs::path& explicitInputPath,
    const TraceSink* trace) {
    InputProcessResult res;

    if (trace) {
        for (const auto& s : def.structures) {
            fs::path p = resolveInputPath(mameRoot, requestedGame, s, explicitInputPath);
            trace->line("TRACE: potential file containing hiscore: " + p.string());
        }
    }

    std::string seedKind;
    for (const auto& s : def.structures) {
        fs::path p = resolveInputPath(mameRoot, requestedGame, s, explicitInputPath);
        if (fs::exists(p)) {
            seedKind = normalizedFileKind(s);
            break;
        }
    }

    for (const auto& s : def.structures) {
        const std::string kind = normalizedFileKind(s);
        if (!seedKind.empty() && seedKind != ".hi" && !Utils::ieq(kind, seedKind)) {
            continue;
        }

        fs::path p = resolveInputPath(mameRoot, requestedGame, s, explicitInputPath);

        std::vector<uint8_t> raw;
        if (!Utils::readFileBytes(p, raw)) continue;

        const bool hasDefinitionChecks = !s.checkAll.empty() || !s.checkAny.empty();

        // size gate (if present). Official hi2txt lets definition bytes select
        // among old/new hiscore layouts even when the dumped file has extra bytes.
        if (!hasDefinitionChecks && !s.checkSizes.empty()) {
            bool ok = false;
            for (int sz : s.checkSizes) {
                if (sz > 0 && (int)raw.size() == sz) {
                    ok = true;
                    if (trace) trace->line("TRACE: matching structure: size = " + std::to_string(sz));
                    break;
                }
            }
            if (!ok) continue;
        }
        else if (!s.checkSizes.empty()) {
            for (int sz : s.checkSizes) {
                if (sz > 0 && (int)raw.size() == sz) {
                    if (trace) trace->line("TRACE: matching structure: size = " + std::to_string(sz));
                    break;
                }
            }
        }

        // defs gate (if present)
        if (hasDefinitionChecks) {
            bool ok = Processor::checkMatches(raw, s);
            if (!ok && s.byteSwap > 1) {
                auto swapped = StructureSelector::applyStructByteSwap(raw, s.byteSwap);
                ok = Processor::checkMatches(swapped, s);
            }
            if (!ok) continue;
        }

        try {
            std::vector<uint8_t> bytes = StructureSelector::applyStructByteSwap(raw, s.byteSwap);
            res.ok = true;
            if (res.inputPath.empty()) res.inputPath = p;
            res.outputId = s.outputId;
            if (trace) trace->line("TRACE: data taken from file: " + p.string());
            mergeRows(res.rows, Processor::extractRows(bytes, s, def, trace));
        }
        catch (const std::exception& e) {
            res.ok = false;
            res.error = e.what();
            return res;
        }
    }

    if (!res.ok) {
        res.error = "No matching structure found under " + mameRoot.string();
    }
    return res;
}

} // namespace openhi2txt
