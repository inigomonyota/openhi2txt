#pragma once
#include "core/Types.h"
#include <vector>

namespace openhi2txt {

class StructureSelector {
public:
    static std::vector<uint8_t> applyStructByteSwap(const std::vector<uint8_t>& raw, int byteSwap);

    // Select which Structure to use among candidates for a given raw buffer.
    // Matches hi2txt-ish selection rules:
    // 1) full check (size + defs), testing both raw and swapped
    // 2) if any candidate has <size>, allow size-only fallback among sized; else (if unsized exist) first unsized
    // 3) otherwise first candidate

};

} // namespace openhi2txt
