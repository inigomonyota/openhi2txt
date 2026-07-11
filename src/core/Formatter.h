#pragma once
#include <string>
#include <unordered_map>
#include "core/Types.h"

namespace openhi2txt {

class Formatter {
public:
    static Value apply(const std::unordered_map<std::string, FormatDef>& formats,
        const std::string& fmtExpr,
        const std::unordered_map<std::string, Value>& row,
        const Value& in,
        int loopIndex = -1);
};

}