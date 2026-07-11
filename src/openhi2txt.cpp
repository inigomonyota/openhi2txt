#include "openhi2txt/openhi2txt.h"

#include "app/DefResolver.h"
#include "app/InputProcessor.h"
#include "core/ResultRenderer.h"
#include "io/ArchiveManager.h"
#include "io/Utils.h"
#include "xml/EntityMapper.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rapidxml.hpp"

namespace fs = std::filesystem;

namespace openhi2txt {

namespace {

static fs::path resolveInputPath(const fs::path& mameRoot,
                                 const std::string& requestedGame,
                                 const Structure& s) {
    const std::string kind = Utils::trim(s.fileKind.empty() ? ".hi" : s.fileKind);

    if (kind == ".hi") {
        return mameRoot / "hiscore" / (requestedGame + ".hi");
    }

    if (!kind.empty() && kind[0] != '.') {
        return mameRoot / "nvram" / requestedGame / kind;
    }

    return mameRoot / "hiscore" / (requestedGame + kind);
}

static std::string toLowerAscii(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

static std::string nodeText(rapidxml::xml_node<>* n) {
    if (!n || !n->value()) return "";
    return EntityMapper::resolve(std::string(n->value(), n->value_size()));
}

static std::string attrText(rapidxml::xml_node<>* n, const char* name) {
    if (!n) return "";
    auto* a = n->first_attribute(name);
    if (!a || !a->value()) return "";
    return EntityMapper::resolve(std::string(a->value(), a->value_size()));
}

static void appendXmlEscaped(std::ostringstream& out, const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        switch (c) {
            case '&': {
                const size_t semi = s.find(';', i + 1);
                if (semi != std::string::npos) {
                    bool entity = false;
                    const std::string name = s.substr(i + 1, semi - i - 1);
                    if (!name.empty() && name.size() <= 16) {
                        entity = name == "amp" || name == "lt" || name == "gt" ||
                                 name == "quot" || name == "apos" || name[0] == '#';
                    }
                    if (entity) {
                        out << s.substr(i, semi - i + 1);
                        i = semi;
                        break;
                    }
                }
                out << "&amp;";
                break;
            }
            case '<': out << "&lt;"; break;
            case '>': out << "&gt;"; break;
            case '"': out << "&quot;"; break;
            case '\'': out << "&apos;"; break;
            default: out << c; break;
        }
    }
}

static std::string renderResultXml(const HiScoreResult& result) {
    std::ostringstream out;
    out << "<hi2txt>\n";

    for (const auto& table : result.tables) {
        if (!table.id.empty()) {
            out << "  <table id=\"";
            appendXmlEscaped(out, table.id);
            out << "\">\n";
        }
        else {
            out << "  <table>\n";
        }

        out << "    ";
        for (const auto& col : table.columns) {
            out << "<col>";
            appendXmlEscaped(out, col);
            out << "</col>";
        }
        out << "\n";

        for (const auto& row : table.rows) {
            out << "    <row>";
            for (const auto& cell : row) {
                out << "<cell>";
                appendXmlEscaped(out, cell);
                out << "</cell>";
            }
            out << "</row>\n";
        }

        out << "  </table>\n";
    }

    for (const auto& field : result.fields) {
        out << "  <field id=\"";
        appendXmlEscaped(out, field.id);
        out << "\">";
        appendXmlEscaped(out, field.value);
        out << "</field>\n";
    }

    out << "</hi2txt>\n";
    return out.str();
}

static bool parseScoreXml(const std::string& gameName,
                          const std::string& xml,
                          ScoreSource source,
                          HiScoreResult& result) {
    result = {};
    result.game = gameName;
    result.source = source;

    std::vector<char> buf(xml.begin(), xml.end());
    buf.push_back('\0');

    rapidxml::xml_document<> doc;
    try {
        doc.parse<rapidxml::parse_non_destructive>(&buf[0]);
    }
    catch (const std::exception& e) {
        result.ok = false;
        result.error = e.what();
        return false;
    }

    auto* root = doc.first_node("hi2txt");
    if (!root) {
        result.ok = false;
        result.error = "Default score XML has no hi2txt root";
        return false;
    }

    for (auto* tableNode = root->first_node("table"); tableNode; tableNode = tableNode->next_sibling("table")) {
        HiScoreTable table;
        table.id = attrText(tableNode, "id");

        for (auto* colNode = tableNode->first_node("col"); colNode; colNode = colNode->next_sibling("col")) {
            std::string column = nodeText(colNode);
            table.columns.push_back(column);
            table.columnInfo.push_back(HiScoreColumn{ column, column, DisplayLevel::Always });
        }

        for (auto* rowNode = tableNode->first_node("row"); rowNode; rowNode = rowNode->next_sibling("row")) {
            std::vector<std::string> row;
            for (auto* cellNode = rowNode->first_node("cell"); cellNode; cellNode = cellNode->next_sibling("cell")) {
                row.push_back(nodeText(cellNode));
            }
            table.rows.push_back(std::move(row));
        }

        result.tables.push_back(std::move(table));
    }

    for (auto* fieldNode = root->first_node("field"); fieldNode; fieldNode = fieldNode->next_sibling("field")) {
        HiScoreField field;
        field.id = attrText(fieldNode, "id");
        field.value = nodeText(fieldNode);
        field.source = field.id;
        field.display = DisplayLevel::Always;
        result.fields.push_back(std::move(field));
    }

    result.ok = true;
    return true;
}

static std::string xorObfuscate(const std::string& data, const std::string& key) {
    if (key.empty()) return data;

    std::string result = data;
    for (size_t i = 0; i < data.size(); ++i) {
        result[i] = static_cast<char>(data[i] ^ key[i % key.size()]);
    }
    return result;
}

static bool decodeXmlWithOptions(const DefaultScoreOptions& options,
                                 const std::string& label,
                                 std::string& xml,
                                 std::string& error) {
    switch (options.obfuscation) {
        case ObfuscationMode::None:
            return true;

        case ObfuscationMode::Xor:
            if (options.key.empty()) {
                error = label + " XOR obfuscation requires a non-empty key";
                return false;
            }
            xml = xorObfuscate(xml, options.key);
            return true;
    }

    error = "Unknown " + label + " obfuscation mode";
    return false;
}

static bool readWholeFile(const fs::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

static bool writeWholeFileIfChanged(const fs::path& path,
                                    const std::string& content,
                                    std::string& error) {
    std::string existing;
    if (readWholeFile(path, existing) && existing == content) return true;

    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "Unable to create score cache directory: " + ec.message();
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Unable to write score cache XML: " + path.string();
        return false;
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file) {
        error = "Unable to finish writing score cache XML: " + path.string();
        return false;
    }

    return true;
}

static fs::path scoreCachePath(const ContextOptions& options, const std::string& gameName) {
    return fs::path(options.scoresDirectory) / (gameName + ".xml");
}

static std::vector<std::string> xmlBasenamesToGameNames(std::vector<std::string> basenames) {
    std::vector<std::string> games;
    games.reserve(basenames.size());

    for (const auto& base : basenames) {
        fs::path p(base);
        std::string game = p.stem().string();
        if (!game.empty()) games.push_back(std::move(game));
    }

    return games;
}

} // namespace

Context::Context(ContextOptions options)
    : options_(std::move(options)) {
}

std::vector<std::string> Context::listGames() const {
    if (options_.definitionsZip.empty()) return {};
    return xmlBasenamesToGameNames(ArchiveManager::listBasenames(fs::path(options_.definitionsZip), ".xml"));
}

std::vector<std::string> Context::listDefaultGames() const {
    if (options_.defaultsZip.empty()) return {};
    return xmlBasenamesToGameNames(ArchiveManager::listBasenames(fs::path(options_.defaultsZip), ".xml"));
}

HiScoreResult Context::readGame(const std::string& gameName,
                                const ReadOptions& readOptions) const {
    HiScoreResult result;
    result.game = gameName;

    const fs::path defaultsZip(options_.defaultsZip);

    if (!options_.scoresDirectory.empty()) {
        const fs::path cachePath = scoreCachePath(options_, gameName);
        std::string cachedXml;
        if (readWholeFile(cachePath, cachedXml)) {
            std::string decodeError;
            HiScoreResult cached;
            if (!decodeXmlWithOptions(options_.scoreCache, "Score cache", cachedXml, decodeError)) {
                cached.ok = false;
                cached.game = gameName;
                cached.error = decodeError;
                cached.usedInputPath = cachePath.string();
                cached.source = ScoreSource::SavedCache;
                return cached;
            }

            if (parseScoreXml(gameName, cachedXml, ScoreSource::SavedCache, cached)) {
                cached.usedInputPath = cachePath.string();
                return cached;
            }
        }
    }

    if (readOptions.useDefaultFallback && !options_.defaultsZip.empty()) {
        const std::string cacheKey = toLowerAscii(gameName);
        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = defaultCache_.find(cacheKey);
            if (it != defaultCache_.end()) return it->second;
            if (defaultMisses_.find(cacheKey) != defaultMisses_.end()) {
                result.ok = false;
                result.error = "No saved or default score XML found for " + gameName;
                result.source = ScoreSource::None;
                return result;
            }
        }

        std::string defaultXml;
        if (ArchiveManager::extractBest(defaultsZip, gameName + ".xml", defaultXml)) {
            HiScoreResult fallback;
            std::string decodeError;
            if (!decodeXmlWithOptions(options_.defaults, "Default score", defaultXml, decodeError)) {
                fallback.ok = false;
                fallback.game = gameName;
                fallback.source = ScoreSource::DefaultFallback;
                fallback.usedInputPath = options_.defaultsZip;
                fallback.error = decodeError;
                return fallback;
            }

            if (parseScoreXml(gameName, defaultXml, ScoreSource::DefaultFallback, fallback)) {
                fallback.usedInputPath = options_.defaultsZip;
                {
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    defaultCache_[cacheKey] = fallback;
                }
                return fallback;
            }
        }

        {
            std::lock_guard<std::mutex> lock(cacheMutex_);
            defaultMisses_.insert(cacheKey);
        }
    }

    result.ok = false;
    result.error = "No saved or default score XML found for " + gameName;
    result.source = ScoreSource::None;
    return result;
}

std::unordered_map<std::string, HiScoreResult> Context::readAllPersistedGames(
    const ReadOptions& readOptions) const {
    std::unordered_map<std::string, HiScoreResult> results;

    auto ingestEntries =
        [&](const std::vector<ArchiveEntry>& entries,
            const DefaultScoreOptions& obfuscation,
            ScoreSource source,
            const std::string& label,
            const std::string& inputPath) {
            for (const auto& entry : entries) {
                const std::string gameName = fs::path(entry.basename).stem().string();
                if (gameName.empty()) continue;

                std::string content = entry.content;
                std::string decodeError;
                if (!decodeXmlWithOptions(obfuscation, label, content, decodeError)) {
                    continue;
                }

                HiScoreResult parsed;
                if (!parseScoreXml(gameName, content, source, parsed)) {
                    continue;
                }

                parsed.usedInputPath = inputPath;
                results[gameName] = parsed;

                if (source == ScoreSource::DefaultFallback) {
                    const std::string cacheKey = toLowerAscii(gameName);
                    std::lock_guard<std::mutex> lock(cacheMutex_);
                    defaultCache_[cacheKey] = parsed;
                    defaultMisses_.erase(cacheKey);
                }
            }
        };

    if (readOptions.useDefaultFallback && !options_.defaultsZip.empty()) {
        ingestEntries(
            ArchiveManager::extractAllBest(fs::path(options_.defaultsZip), ".xml"),
            options_.defaults,
            ScoreSource::DefaultFallback,
            "Default score",
            options_.defaultsZip
        );
    }

    if (!options_.scoresDirectory.empty()) {
        ingestEntries(
            ArchiveManager::extractAllBest(fs::path(options_.scoresDirectory), ".xml"),
            options_.scoreCache,
            ScoreSource::SavedCache,
            "Score cache",
            options_.scoresDirectory
        );
    }

    return results;
}

HiScoreResult Context::refreshGame(const std::string& gameName,
                                   const ReadOptions& readOptions) const {
    HiScoreResult result;
    result.game = gameName;

    const fs::path definitionsZip(options_.definitionsZip);
    const fs::path mameRoot(options_.mameRoot);

    auto defRes = DefResolver::loadFromZip(definitionsZip, mameRoot, gameName);
    if (!defRes.ok) {
        result.ok = false;
        result.error = defRes.error;
        return result;
    }

    auto inRes = InputProcessor::process(mameRoot, gameName, defRes.def);
    if (!inRes.ok) {
        result.ok = false;
        result.error = inRes.error;
        result.usedDefinition = defRes.usedDefId;
        result.source = ScoreSource::None;
        return result;
    }

    result = ResultRenderer::render(defRes.def, inRes.rows, inRes.outputId, readOptions);
    result.game = gameName;
    result.usedDefinition = defRes.usedDefId;
    result.usedInputPath = inRes.inputPath.string();
    result.source = ScoreSource::RealInput;

    if (!options_.scoresDirectory.empty()) {
        std::string cacheXml = renderResultXml(result);
        std::string encodeError;
        if (decodeXmlWithOptions(options_.scoreCache, "Score cache", cacheXml, encodeError)) {
            std::string writeError;
            if (!writeWholeFileIfChanged(scoreCachePath(options_, gameName), cacheXml, writeError)) {
                result.warnings.push_back(writeError);
            }
        }
        else {
            result.warnings.push_back(encodeError);
        }
    }

    return result;
}

bool Context::hasInputForGame(const std::string& gameName) const {
    auto defRes = DefResolver::loadFromZip(fs::path(options_.definitionsZip), fs::path(options_.mameRoot), gameName);
    if (!defRes.ok) return false;

    for (const auto& s : defRes.def.structures) {
        if (fs::exists(resolveInputPath(fs::path(options_.mameRoot), gameName, s))) return true;
    }

    return false;
}

bool Context::hasDefaultForGame(const std::string& gameName) const {
    if (options_.defaultsZip.empty()) return false;

    const std::string cacheKey = toLowerAscii(gameName);
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (defaultCache_.find(cacheKey) != defaultCache_.end()) return true;
        if (defaultMisses_.find(cacheKey) != defaultMisses_.end()) return false;
    }

    std::string defaultXml;
    const bool found = ArchiveManager::extractBest(fs::path(options_.defaultsZip), gameName + ".xml", defaultXml);
    if (!found) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        defaultMisses_.insert(cacheKey);
    }
    return found;
}

} // namespace openhi2txt
