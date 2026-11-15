#include "app/DefResolver.h"
#include "core/Trace.h"
#include "io/ArchiveManager.h"
#include "io/HiscoreDat.h"
#include "io/Utils.h"
#include "xml/XmlParser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace openhi2txt {

namespace {

std::string normalizeHiscoreDatLine(const std::string& line) {
    std::string t = Utils::trim(line);
    if (t.empty()) return "";

    const size_t semi = t.find(';');
    if (semi != std::string::npos) t = Utils::trim(t.substr(0, semi));

    std::replace(t.begin(), t.end(), ',', ':');

    std::vector<std::string> parts;
    std::string cur;
    std::stringstream ss(t);
    while (std::getline(ss, cur, ':')) parts.push_back(Utils::trim(cur));

    size_t firstDefinitionPart = 0;
    if (t.rfind("@:", 0) == 0) {
        if (parts.size() < 6) return "";
        firstDefinitionPart = parts.size() - 4;
    }
    else {
        if (parts.size() != 5) return "";
        firstDefinitionPart = 1;
    }

    std::string out;
    for (size_t i = firstDefinitionPart; i < parts.size(); ++i) {
        if (i > firstDefinitionPart) out += ":";
        std::string p = parts[i];
        if (p.size() >= 2 && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) p = p.substr(2);
        if (p.empty()) return "";
        for (char& ch : p) ch = (char)std::tolower((unsigned char)ch);
        out += p;
    }
    return out;
}

std::vector<std::string> normalizedHiscoreDatDefinitions(const HiscoreDat::Block& block) {
    std::vector<std::string> out;
    for (const auto& line : block.bodyLines) {
        std::string norm = normalizeHiscoreDatLine(line);
        if (!norm.empty()) out.push_back(std::move(norm));
    }
    return out;
}

bool definitionTokensMatch(const std::vector<std::string>& structureTokens,
                           const std::vector<std::string>& datTokens) {
    if (structureTokens.empty()) return true;
    if (datTokens.empty()) return true;
    if (structureTokens.size() < datTokens.size()) return false;

    for (size_t start = 0; start + datTokens.size() <= structureTokens.size(); ++start) {
        bool matched = true;
        for (size_t i = 0; i < datTokens.size(); ++i) {
            if (structureTokens[start + i] != datTokens[i]) {
                matched = false;
                break;
            }
        }
        if (matched) return true;
    }

    return false;
}

std::string joinDefinitionTokens(const std::vector<std::string>& tokens) {
    std::string joined;
    for (const auto& tok : tokens) joined += tok;
    return joined;
}

void filterStructuresByHiscoreDat(GameDef& def,
                                  const std::vector<std::string>& datTokens,
                                  const TraceSink* trace) {
    if (datTokens.empty()) return;

    std::vector<Structure> filtered;
    filtered.reserve(def.structures.size());
    const std::string joinedDat = joinDefinitionTokens(datTokens);
    for (auto& s : def.structures) {
        if (definitionTokensMatch(s.hiscoreDefinitionTokens, datTokens)) {
            if (trace && !s.hiscoreDefinitionTokens.empty()) {
                trace->line("TRACE: matching structure: hiscore.dat = " + joinedDat);
            }
            filtered.push_back(std::move(s));
        }
        else if (trace && !s.hiscoreDefinitionTokens.empty()) {
            trace->line("TRACE: structure definition " + joinDefinitionTokens(s.hiscoreDefinitionTokens) +
                        " does NOT match hiscore.dat definition " + joinedDat);
        }
    }

    def.structures = std::move(filtered);
}

std::string definitionCacheKey(const fs::path& definitions,
                               const fs::path& hiscoreDat,
                               const std::string& requestedGame) {
    const auto describePath = [](const fs::path& path) {
        std::error_code error;
        const fs::path canonical = fs::weakly_canonical(path, error);
        const std::string name = (error ? path.lexically_normal() : canonical).string();
        error.clear();
        const auto modified = fs::last_write_time(path, error);
        const auto ticks = error ? 0 : modified.time_since_epoch().count();
        error.clear();
        const auto size = fs::is_regular_file(path, error) ? fs::file_size(path, error) : 0;
        return name + "\x1f" + std::to_string(ticks) + "\x1f" +
            std::to_string(error ? 0 : size);
    };
    std::string lowered = requestedGame;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return describePath(definitions) + "\x1e" + describePath(hiscoreDat) + "\x1e" + lowered;
}

} // namespace

static DefLoadResult loadFromZipUncached(const fs::path& defsZip,
                                         const fs::path& mameRoot,
                                         const std::string& requestedGame,
                                         const fs::path& hiscoreDatOverride,
                                         const TraceSink* trace) {
    DefLoadResult res;

    const fs::path hiscoreDat = hiscoreDatOverride.empty()
        ? (mameRoot / "plugins" / "hiscore" / "hiscore.dat")
        : hiscoreDatOverride;
    const HiscoreDat::Block hiscoreBlock = HiscoreDat::findBlockForGame(hiscoreDat, requestedGame);
    const std::vector<std::string> hiscoreDefinitionTokens = normalizedHiscoreDatDefinitions(hiscoreBlock);

    // Build def-candidates: requested game + hiscore.dat alias family
    std::vector<std::string> defCandidates;
    defCandidates.reserve(128);
    defCandidates.push_back(requestedGame);

    {
        for (const auto& alias : hiscoreBlock.labels) {
            if (!Utils::ieq(alias, requestedGame))
                defCandidates.push_back(alias);
        }
    }

    // Try to load first USABLE xml def (has structures + at least one output table),
    // and chase <sameas> before parsing.
    for (const auto& cand : defCandidates) {
        std::string tmp;
        if (!ArchiveManager::extractBest(defsZip, cand + ".xml", tmp))
            continue;

        if (trace) {
            trace->line("TRACE: reading a description from file: " + defsZip.string() + ", entry " + cand + ".xml");
            if (!Utils::ieq(XmlParser::getRootName(tmp), "openhi2txt")) {
                trace->line("TRACE: reading dtd from file: " + defsZip.string() + ", entry hi2txt.dtd");
            }
        }

        // chase <sameas id="..."> (bounded)
        for (int hop = 0; hop < 16; ++hop) {
            std::string sa = XmlParser::getSameAsId(tmp);
            if (sa.empty()) break;
            std::string nextXml;
            if (!ArchiveManager::extractBest(defsZip, sa + ".xml", nextXml)) break;
            if (trace) trace->line("TRACE: reading a description from file: " + defsZip.string() + ", entry " + sa + ".xml");
            tmp = std::move(nextXml);
        }

        XmlParseResult parsedRes = XmlParser::parseWithDiagnostics(tmp);
        if (!parsedRes.ok) {
            res.ok = false;
            res.errorKind = HiScoreErrorKind::DefinitionInvalid;
            if (parsedRes.errorKind == XmlParseErrorKind::VersionRequirement) {
                res.error = "ERROR: " + parsedRes.error;
            }
            else {
                res.error = "ERROR: unable to find DTD file: " + parsedRes.error + "\n" +
                            "ERROR: unable to find DTD file: " + parsedRes.error + "\n" +
                            "ERROR: No content inside XML description for ROM '" + requestedGame + "'";
            }
            return res;
        }

        GameDef parsed = std::move(parsedRes.def);
        if (trace) {
            std::vector<std::string> formatIds;
            formatIds.reserve(parsed.formats.size());
            for (const auto& kv : parsed.formats) formatIds.push_back(kv.first);
            std::sort(formatIds.begin(), formatIds.end());
            for (const auto& id : formatIds) {
                trace->line("TRACE: format defined: " + id);
            }
            trace->line("TRACE: format auto: hexadecimal_string");
            std::vector<std::string> charsetIds;
            charsetIds.reserve(parsed.charsets.size());
            for (const auto& kv : parsed.charsets) charsetIds.push_back(kv.first);
            std::sort(charsetIds.begin(), charsetIds.end());
            for (const auto& id : charsetIds) {
                trace->line("TRACE: charset defined: " + id);
            }
            if (std::none_of(charsetIds.begin(), charsetIds.end(),
                    [](const std::string& id) { return Utils::ieq(id, "CS_NUMBER"); })) {
                trace->line("TRACE: charset defined: CS_NUMBER");
            }
        }

        filterStructuresByHiscoreDat(parsed, hiscoreDefinitionTokens, trace);

        if (Utils::ieq(cand, requestedGame) && parsed.structures.empty()) {
            res.ok = false;
            res.errorKind = HiScoreErrorKind::StructureNotMatched;
            res.error =
                "ERROR: unable to find a structure from the xml definition that matches size and "
                "hiscore.dat definition' for game '" + requestedGame + "'";
            return res;
        }

        bool hasAnyOutput = false;
        for (const auto& kv : parsed.outputs) {
            const auto& out = kv.second;
            if (!out.tables.empty() || !out.fields.empty()) { hasAnyOutput = true; break; }
        }

        if (!parsed.structures.empty() && !hasAnyOutput) {
            res.ok = false;
            res.errorKind = HiScoreErrorKind::OutputNotFound;
            res.error = "ERROR: unable to find an output from the xml file that matches the structure' for game '" +
                        requestedGame + "'";
            return res;
        }

        if (!parsed.structures.empty() && hasAnyOutput) {
            res.ok = true;
            res.xmlText = std::move(tmp);
            res.usedDefId = parsed.id.empty() ? cand : parsed.id;
            res.def = std::move(parsed);
            return res;
        }
    }

    res.ok = false;
    res.errorKind = HiScoreErrorKind::DefinitionNotFound;
    res.error = "ERROR: No XML description found for ROM '" + requestedGame + "'";
    return res;
}

DefLoadResult DefResolver::loadFromZip(const fs::path& defsZip,
                                       const fs::path& mameRoot,
                                       const std::string& requestedGame,
                                       const fs::path& hiscoreDatOverride,
                                       const TraceSink* trace) {
    if (trace) {
        return loadFromZipUncached(
            defsZip, mameRoot, requestedGame, hiscoreDatOverride, trace);
    }

    const fs::path hiscoreDat = hiscoreDatOverride.empty()
        ? (mameRoot / "plugins" / "hiscore" / "hiscore.dat")
        : hiscoreDatOverride;
    const std::string key = definitionCacheKey(defsZip, hiscoreDat, requestedGame);
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, DefLoadResult> cache;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        const auto cached = cache.find(key);
        if (cached != cache.end()) return cached->second;
    }

    DefLoadResult result = loadFromZipUncached(
        defsZip, mameRoot, requestedGame, hiscoreDatOverride, nullptr);
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        cache.emplace(key, result);
    }
    return result;
}

} // namespace openhi2txt
