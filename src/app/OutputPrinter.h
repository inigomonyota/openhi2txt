#pragma once
#include "core/HiScoreResult.h"

namespace openhi2txt {

enum class OutputFormat {
    Text,
    Xml
};

class OutputPrinter {
public:
    static void print(const HiScoreResult& result, OutputFormat format);
};

} // namespace openhi2txt
