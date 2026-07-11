#pragma once
#include "core/Types.h"
#include <filesystem>
#include <string>

namespace openhi2txt {

struct TraceSink;

struct DefLoadResult {
    GameDef def;
    std::string usedDefId;   // which xml id we ended up using (may be alias / sameas)
    std::string xmlText;     // resolved xml that was parsed (after sameas chase)
    bool ok = false;
    std::string error;
};

class DefResolver {
public:
    static DefLoadResult loadFromZip(const std::filesystem::path& defsZip,
                                     const std::filesystem::path& mameRoot,
                                     const std::string& requestedGame,
                                     const std::filesystem::path& hiscoreDat = {},
                                     const TraceSink* trace = nullptr);
};

} // namespace openhi2txt
