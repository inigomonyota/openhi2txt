#pragma once
#include "core/Types.h"
#include "openhi2txt/openhi2txt.h"
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>

namespace openhi2txt {

struct TraceSink;

struct InputProcessResult {
    std::vector<std::unordered_map<std::string, Value>> rows;
    std::string outputId;
    std::filesystem::path inputPath;
    bool ok = false;
    std::string error;
    HiScoreErrorKind errorKind = HiScoreErrorKind::None;
};

class InputProcessor {
public:
    static InputProcessResult process(const std::filesystem::path& mameRoot,
                                      const std::string& requestedGame,
                                      const GameDef& def,
                                      const std::filesystem::path& explicitInputPath = {},
                                      const TraceSink* trace = nullptr);
};

} // namespace openhi2txt
