#include "app/DefResolver.h"
#include "core/Trace.h"
#include "io/ArchiveManager.h"
#include "io/HiscoreDat.h"
#include "io/Utils.h"
#include "xml/XmlParser.h"

#include <algorithm>
#include <cctype>
#include <sstream>
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
    if (t.rfind("@:", 0) != 0) return "";

    std::replace(t.begin(), t.end(), ',', ':');

    std::vector<std::string> parts;
    std::string cur;
    std::stringstream ss(t);
    while (std::getline(ss, cur, ':')) parts.push_back(Utils::trim(cur));

    if (parts.size() < 6) return "";

    std::string out;
    for (size_t i = parts.size() - 4; i < parts.size(); ++i) {
        if (i > parts.size() - 4) out += ":";
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

} // namespace

DefLoadResult DefResolver::loadFromZip(const fs::path& defsZip,
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
        auto aliases = HiscoreDat::aliasesForGame(hiscoreDat, requestedGame);
        for (const auto& a : aliases) {
            if (!Utils::ieq(a, requestedGame))
                defCandidates.push_back(a);
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
            trace->line("TRACE: reading dtd from file: " + defsZip.string() + ", entry hi2txt.dtd");
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
            res.error = "ERROR: unable to find DTD file: " + parsedRes.error + "\n" +
                        "ERROR: unable to find DTD file: " + parsedRes.error + "\n" +
                        "ERROR: No content inside XML description for ROM '" + requestedGame + "'";
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
            if (std::find(charsetIds.begin(), charsetIds.end(), "CS_NUMBER") == charsetIds.end()) {
                trace->line("TRACE: charset defined: CS_NUMBER");
            }
        }

        filterStructuresByHiscoreDat(parsed, hiscoreDefinitionTokens, trace);

        if (Utils::ieq(cand, requestedGame) && parsed.structures.empty()) {
            res.ok = true;
            res.xmlText = std::move(tmp);
            res.def = std::move(parsed);
            res.usedDefId = cand;
            return res;
        }

        bool hasAnyOutput = false;
        for (const auto& kv : parsed.outputs) {
            const auto& out = kv.second;
            if (!out.tables.empty() || !out.fields.empty()) { hasAnyOutput = true; break; }
        }

        if (!parsed.structures.empty() && !hasAnyOutput) {
            res.ok = false;
            res.error = "ERROR: unable to find an output from the xml file that matches the structure' for game '" +
                        requestedGame + "'";
            return res;
        }

        if (!parsed.structures.empty() && hasAnyOutput) {
            res.ok = true;
            res.xmlText = std::move(tmp);
            res.def = std::move(parsed);
            res.usedDefId = cand;
            return res;
        }
    }

    res.ok = false;
    res.error = "No usable XML def found for " + requestedGame +
                " (searched requested game + hiscore.dat alias family)";
    return res;
}

} // namespace openhi2txt
