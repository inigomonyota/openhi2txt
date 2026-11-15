#pragma once

#include "core/Types.h"
#include "openhi2txt/openhi2txt.h"

#include <vector>

namespace openhi2txt {

class InputPlanner {
public:
    static std::vector<HiScoreInputPlan> plan(const GameDef& def);
};

} // namespace openhi2txt
