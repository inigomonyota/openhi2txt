#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <openhi2txt/version.h>

namespace openhi2txt {

enum class ScoreSource {
    None,
    RealInput,
    SavedCache,
    DefaultFallback
};

enum class HiScoreErrorKind {
    None,
    DefinitionNotFound,
    DefinitionInvalid,
    StructureNotMatched,
    OutputNotFound,
    InputNotFound,
    InvalidData,
    ScoreXmlNotFound,
    ScoreXmlInvalid,
    ConfigurationError,
    Unknown
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

enum class HiScoreOutputKind {
    Table,
    Field
};

struct HiScoreOutputItem {
    HiScoreOutputKind kind = HiScoreOutputKind::Table;
    size_t index = 0;
};

struct HiScoreResult {
    bool ok = false;
    std::string error;
    HiScoreErrorKind errorKind = HiScoreErrorKind::None;

    std::string game;
    std::string usedDefinition;
    std::string usedInputPath;
    ScoreSource source = ScoreSource::None;

    std::vector<HiScoreTable> tables;
    std::vector<HiScoreField> fields;
    std::vector<std::string> warnings;
    std::vector<HiScoreOutputItem> outputOrder;
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

struct HiScoreInput {
    // Matches structure@file in the game definition. Empty and "hi" are
    // treated as aliases for ".hi".
    std::string fileKind = ".hi";
    std::vector<std::uint8_t> bytes;

    // Optional diagnostic identity for HiScoreResult::usedInputPath.
    std::string sourceName;
};

struct HiScoreWatchRange {
    // Offset in the live/persistent source exposed by MAME. For a logical
    // disk source this is an absolute byte offset, including the XML window.
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
};

struct HiScoreInputPlan {
    // Index of the alternative <structure> in the resolved definition.
    std::size_t structureIndex = 0;
    std::string fileKind = ".hi";
    std::string outputId;

    // Exact accepted decoder-buffer sizes, when the definition declares them.
    std::vector<std::uint64_t> acceptedBufferSizes;

    // Logical source window used by container-backed inputs such as DIF/CHD.
    std::uint64_t sourceWindowOffset = 0;
    std::uint64_t sourceWindowLength = 0;

    // Conservative watch ranges. For ordinary .hi and NVRAM sources, MAME can
    // watch these ranges but send the complete source image after stabilization.
    std::vector<HiScoreWatchRange> watchRanges;
};

struct HiScoreInputPlanResult {
    bool ok = false;
    std::string error;
    HiScoreErrorKind errorKind = HiScoreErrorKind::None;
    std::string game;
    std::string usedDefinition;
    std::vector<HiScoreInputPlan> inputs;
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

    // Decode caller-provided source bytes without reading MAME's score files.
    // For a DIF structure, bytes contains the structure's logical disk window,
    // not the CHD or DIF container.
    HiScoreResult decodeGame(const std::string& gameName,
                             const std::vector<HiScoreInput>& inputs,
                             const ReadOptions& options = {}) const;

    // Describe the persistent-source bytes which can affect decoded output.
    // One entry is returned for each alternative structure in the definition.
    HiScoreInputPlanResult planGameInputs(const std::string& gameName) const;

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
