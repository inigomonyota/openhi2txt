#include "core/StructureSelector.h"
#include "core/Processor.h"

#include <algorithm>
#include <cstddef>

namespace openhi2txt {

std::vector<uint8_t> StructureSelector::applyStructByteSwap(const std::vector<uint8_t>& raw, int byteSwap) {
    if (byteSwap <= 1) return raw;
    std::vector<uint8_t> out = raw;
    const size_t n = out.size();
    const size_t c = (size_t)byteSwap;
    for (size_t i = 0; i + c <= n; i += c) {
        std::reverse(out.begin() + (std::ptrdiff_t)i, out.begin() + (std::ptrdiff_t)(i + c));
    }
    return out;
}


} // namespace openhi2txt
