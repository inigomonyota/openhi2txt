#pragma once

#include "openhi2txt/openhi2txt.h"

#include <string>
#include <vector>

namespace openhi2txt {

struct SparseInputResult {
    bool ok = false;
    std::string error;
    std::vector<HiScoreInput> inputs;
};

class SparseInput {
public:
    static SparseInputResult materialize(const std::vector<HiScoreSparseInput>& inputs);
};

} // namespace openhi2txt
