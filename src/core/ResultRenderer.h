#pragma once

#include "core/HiScoreResult.h"
#include "core/Types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace openhi2txt {

struct TraceSink;

class ResultRenderer {
public:
    static HiScoreResult render(const GameDef& def,
                                const std::vector<std::unordered_map<std::string, Value>>& rows,
                                const std::string& outputId,
                                const ReadOptions& options,
                                const TraceSink* trace = nullptr);
};

} // namespace openhi2txt
