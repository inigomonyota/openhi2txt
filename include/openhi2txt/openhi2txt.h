#pragma once

#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace openhi2txt {

inline constexpr int VersionMajor = 0;
inline constexpr int VersionMinor = 1;
inline constexpr int VersionPatch = 0;
inline constexpr const char* VersionString = "0.1.0";

enum class ScoreSource {
    None,
    RealInput,
    SavedCache,
    DefaultFallback
};

struct ReadOptions {
    bool includeExtra = false;
    bool includeDebug = false;
    bool useDefaultFallback = true;

    std::vector<std::string> keepFields;
    std::vector<std::string> hideFields;
    std::vector<std::pair<std::string, std::string>> keepTableValues;
    bool keepFirstScore = false;
    bool keepFirstTable = false;
    int maxLines = 0;
    int maxColumns = 0;

    bool scoreGrouping = false;
    std::string scoreGroupingSeparator = ".";
    int scoreGroupingSize = 3;
};

enum class DisplayLevel {
    Always,
    Extra,
    Debug
};

struct HiScoreColumn {
    std::string id;
    std::string source;
    DisplayLevel display = DisplayLevel::Always;
};

struct HiScoreTable {
    std::string id;
    DisplayLevel display = DisplayLevel::Always;
    std::vector<std::string> columns;
    std::vector<HiScoreColumn> columnInfo;
    std::vector<std::vector<std::string>> rows;
};

struct HiScoreField {
    std::string id;
    std::string value;
    std::string source;
    DisplayLevel display = DisplayLevel::Always;
};

struct HiScoreResult {
    bool ok = false;
    std::string error;

    std::string game;
    std::string usedDefinition;
    std::string usedInputPath;
    ScoreSource source = ScoreSource::None;

    std::vector<HiScoreTable> tables;
    std::vector<HiScoreField> fields;
    std::vector<std::string> warnings;
};

enum class ObfuscationMode {
    None,
    Xor
};

struct DefaultScoreOptions {
    ObfuscationMode obfuscation = ObfuscationMode::None;
    std::string key;
};

struct ContextOptions {
    std::string definitionsZip;
    std::string defaultsZip;
    std::string scoresDirectory;
    std::string mameRoot;
    DefaultScoreOptions defaults;
    DefaultScoreOptions scoreCache;
};

class Context {
public:
    explicit Context(ContextOptions options);

    HiScoreResult readGame(const std::string& gameName,
                           const ReadOptions& options = {}) const;

    std::unordered_map<std::string, HiScoreResult> readAllPersistedGames(
                           const ReadOptions& options = {}) const;

    HiScoreResult refreshGame(const std::string& gameName,
                              const ReadOptions& options = {}) const;

    std::vector<std::string> listGames() const;
    std::vector<std::string> listDefaultGames() const;

    bool hasInputForGame(const std::string& gameName) const;
    bool hasDefaultForGame(const std::string& gameName) const;

private:
    ContextOptions options_;
    mutable std::mutex cacheMutex_;
    mutable std::unordered_map<std::string, HiScoreResult> defaultCache_;
    mutable std::unordered_set<std::string> defaultMisses_;
};

} // namespace openhi2txt
