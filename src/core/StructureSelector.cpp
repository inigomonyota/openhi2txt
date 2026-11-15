#include "core/StructureSelector.h"
#include "core/Processor.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

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

namespace {

uint16_t rotateRight16(uint16_t value, unsigned count) {
    count &= 15U;
    return static_cast<uint16_t>((value >> count) | (value << ((16U - count) & 15U)));
}

void decodeNamcoSystem12Region(std::vector<uint8_t>& bytes, const DecodeRegion& region) {
    constexpr uint64_t trailerSize = 10;
    if (region.offset > bytes.size() || region.size > bytes.size() - region.offset ||
        trailerSize > bytes.size() - region.offset - region.size) {
        throw std::runtime_error("Namco System 12 decode region exceeds the input file.");
    }

    const size_t offset = static_cast<size_t>(region.offset);
    const size_t size = static_cast<size_t>(region.size);
    const uint16_t stored = static_cast<uint16_t>(bytes[offset + size]) |
        static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + size + 1]) << 8);
    const uint16_t expectedChecksum = static_cast<uint16_t>(rotateRight16(stored, 3) ^ 0xEB7D);

    uint16_t state = expectedChecksum;
    uint16_t checksum = 0xA2F1;
    for (size_t i = 0; i < size; ++i) {
        const uint8_t decoded = static_cast<uint8_t>(bytes[offset + i] ^ (state & 0xFF));
        bytes[offset + i] = decoded;
        checksum = static_cast<uint16_t>(checksum + static_cast<uint16_t>(decoded) * 7U);
        state = static_cast<uint16_t>(static_cast<uint16_t>(rotateRight16(state, 1) * 5U) + 1U);
    }

    if (checksum != expectedChecksum) {
        throw std::runtime_error("Namco System 12 decode checksum mismatch.");
    }
}

} // namespace

std::vector<uint8_t> StructureSelector::applyDecodeRegions(
    const std::vector<uint8_t>& raw,
    const std::vector<DecodeRegion>& regions) {
    if (regions.empty()) return raw;

    std::vector<uint8_t> out = raw;
    for (const auto& region : regions) {
        if (region.type == "namco-system12") {
            decodeNamcoSystem12Region(out, region);
            continue;
        }
        throw std::runtime_error("Unsupported structure decoder: " + region.type);
    }
    return out;
}


} // namespace openhi2txt
