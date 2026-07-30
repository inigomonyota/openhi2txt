// src/core/Processor.cpp
#include "core/Processor.h"
#include "core/Formatter.h"
#include "core/Trace.h"
#include "io/Utils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <cmath>

namespace openhi2txt {

using namespace Utils;

// ------------------------------------------------------------
// Check helpers
// ------------------------------------------------------------
static bool checkDefAt(const std::vector<uint8_t>& buf, const CheckDef& cd) {
    if (cd.offset < 0) return false;
    size_t off = (size_t)cd.offset;
    if (off + cd.bytes.size() > buf.size()) return false;
    return std::memcmp(buf.data() + off, cd.bytes.data(), cd.bytes.size()) == 0;
}

bool Processor::checkMatches(const std::vector<uint8_t>& raw, const Structure& s) {
    if (!s.checkSizes.empty()) {
        bool ok = false;
        for (int sz : s.checkSizes) {
            if (sz > 0 && (int)raw.size() == sz) { ok = true; break; }
        }
        if (!ok) return false;
    }

    for (const auto& cd : s.checkAll) {
        if (!checkDefAt(raw, cd)) return false;
    }

    if (!s.checkAny.empty()) {
        bool any = false;
        for (const auto& cd : s.checkAny) {
            if (checkDefAt(raw, cd)) { any = true; break; }
        }
        if (!any) return false;
    }

    return true;
}

// ------------------------------------------------------------
// Byte/bit transforms
// ------------------------------------------------------------
static void applyEndianness(std::vector<uint8_t>& b, Endianness e) {
    if (e == Endianness::Little) std::reverse(b.begin(), b.end());
}

static void applyChunkReverse(std::vector<uint8_t>& bytes, int chunk) {
    if (chunk <= 1) return;
    const size_t n = bytes.size();
    const size_t c = (size_t)chunk;
    for (size_t i = 0; i + c <= n; i += c) {
        std::reverse(bytes.begin() + (ptrdiff_t)i, bytes.begin() + (ptrdiff_t)(i + c));
    }
}

static uint8_t bitReverse8(uint8_t x) {
    x = (uint8_t)(((x & 0xF0u) >> 4) | ((x & 0x0Fu) << 4));
    x = (uint8_t)(((x & 0xCCu) >> 2) | ((x & 0x33u) << 2));
    x = (uint8_t)(((x & 0xAAu) >> 1) | ((x & 0x55u) << 1));
    return x;
}

static void applyBitSwap(std::vector<uint8_t>& b) {
    for (auto& x : b) x = bitReverse8(x);
}

static void applyByteTrimSentinel(std::vector<uint8_t>& b, uint8_t sentinel) {
    while (!b.empty() && b.front() == sentinel) b.erase(b.begin());
}

static void applyByteTruncSentinel(std::vector<uint8_t>& b, uint8_t sentinel) {
    auto it = std::find(b.begin(), b.end(), sentinel);
    if (it != b.end()) b.erase(it, b.end());
}

static void applyByteSkip(std::vector<uint8_t>& b, const std::string& spec) {
    std::string s = trim(spec);
    if (s.empty()) return;

    if (ieq(s, "odd") || ieq(s, "even")) {
        const bool keepEvenIndex = ieq(s, "even"); // even => keep bytes 0,2,4...
        std::vector<uint8_t> out;
        out.reserve((b.size() + 1) / 2);
        for (size_t i = 0; i < b.size(); ++i) {
            const bool evenIndex = ((i % 2) == 0);
            if ((keepEvenIndex && evenIndex) || (!keepEvenIndex && !evenIndex)) out.push_back(b[i]);
        }
        b.swap(out);
        return;
    }

    const bool binaryMask =
        s.size() > 1 &&
        std::all_of(s.begin(), s.end(), [](char ch) { return ch == '0' || ch == '1'; }) &&
        s.find('1') != std::string::npos;
    if (binaryMask) {
        std::vector<uint8_t> out;
        out.reserve(b.size());
        for (size_t i = 0; i < b.size(); ++i) {
            if (s[i % s.size()] == '1') out.push_back(b[i]);
        }
        b.swap(out);
        return;
    }

    uint8_t sentinel = 0;
    if (parseHexByte0x(s, sentinel)) {
        std::vector<uint8_t> out;
        out.reserve(b.size());
        for (auto x : b) if (x != sentinel) out.push_back(x);
        b.swap(out);
        return;
    }
}

static void applyNibbleSkip(std::vector<uint8_t>& b, const std::string& mode) {
    if (b.empty()) return;

    const bool keepOdd = ieq(mode, "odd");   // keep low nibbles
    const bool keepEven = ieq(mode, "even");  // keep high nibbles
    if (!keepOdd && !keepEven) return;

    auto getNib = [&](int nibIndex) -> uint8_t {
        const int bi = nibIndex / 2;
        const bool isHigh = ((nibIndex % 2) == 0);
        const uint8_t by = b[(size_t)bi];
        return isHigh ? (uint8_t)((by >> 4) & 0xF) : (uint8_t)(by & 0xF);
        };

    const int totalN = (int)b.size() * 2;

    // Collect the kept nibbles in MSB->LSB order (nibIndex 0 is high nibble of byte 0).
    std::vector<uint8_t> kept;
    kept.reserve((size_t)totalN);

    for (int ni = 0; ni < totalN; ++ni) {
        const bool isEvenNib = ((ni % 2) == 0); // even index => high nibble positions
        if (keepEven && isEvenNib) kept.push_back(getNib(ni));
        else if (keepOdd && !isEvenNib) kept.push_back(getNib(ni));
    }

    // hi2txt behavior: if digit-count is odd, pad a 0 on the MOST-significant side.
    if ((kept.size() & 1u) != 0u) {
        kept.insert(kept.begin(), 0);
    }

    if (kept.empty()) { b.clear(); return; }

    // Pack nibbles back into bytes, MSB nibble first.
    std::vector<uint8_t> out;
    out.reserve(kept.size() / 2);

    for (size_t i = 0; i < kept.size(); i += 2) {
        const uint8_t hi = kept[i] & 0xF;
        const uint8_t lo = kept[i + 1] & 0xF;
        out.push_back((uint8_t)((hi << 4) | lo));
    }

    b.swap(out);
}

static void applyNibbleTrim(std::vector<uint8_t>& b, uint8_t nib) {
    if (b.empty()) return;

    auto leadingNibble = [&](const std::vector<uint8_t>& v) -> uint8_t {
        if (v.empty()) return 0;
        return (uint8_t)((v[0] >> 4) & 0xF);
    };

    auto shiftLeftOneNibble = [&](std::vector<uint8_t>& v) {
        uint8_t carry = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            const uint8_t cur = v[i];
            const uint8_t nextCarry = (uint8_t)(cur & 0xF);
            v[i] = (uint8_t)((cur << 4) | carry);
            carry = nextCarry;
        }
    };

    while (!b.empty() && leadingNibble(b) == (nib & 0xF)) {
        shiftLeftOneNibble(b);
        bool any = false;
        for (auto x : b) if (x != 0) { any = true; break; }
        if (!any) break;
    }
}

static uint8_t extractMaskedBits_NoPad_MsbFirst(const std::vector<uint8_t>& src,
    const std::vector<uint8_t>& maskBytes) {
    auto bitAt = [](uint8_t by, int bit) -> int { // bit 0..7 means MSB..LSB
        return (by >> (7 - bit)) & 1;
        };

    uint8_t out = 0;
    const size_t n = std::min(src.size(), maskBytes.size());

    for (size_t i = 0; i < n; ++i) {
        const uint8_t by = src[i];
        const uint8_t m = maskBytes[i];

        for (int bit = 0; bit < 8; ++bit) {
            if (!bitAt(m, bit)) continue;
            out = (uint8_t)((out << 1) | (uint8_t)bitAt(by, bit));
        }
    }

    // Critical: DO NOT shift/align to MSB. Keep nibble in low bits (0..15).
    return out;
}

static std::vector<uint8_t> applyBitmaskPerCharText_NoPad(const std::vector<uint8_t>& src,
    const std::vector<std::vector<uint8_t>>& charMasks) {
    if (src.empty() || charMasks.empty()) return src;

    std::vector<uint8_t> out;
    out.reserve(charMasks.size());
    for (const auto& cm : charMasks) {
        out.push_back(extractMaskedBits_NoPad_MsbFirst(src, cm));
    }
    return out;
}


static std::vector<uint8_t> applyBitmaskPack(const std::vector<uint8_t>& b, const std::vector<uint8_t>& maskBytes) {
    if (b.empty() || maskBytes.empty()) return b;

    auto bitAt = [](uint8_t by, int bit) -> int { // bit 7..0
        return (by >> (7 - bit)) & 1;
    };

    std::vector<uint8_t> out;
    uint8_t cur = 0;
    int curBits = 0;

    const size_t n = std::min(b.size(), maskBytes.size());
    for (size_t i = 0; i < n; ++i) {
        const uint8_t by = b[i];
        const uint8_t m = maskBytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            const int use = bitAt(m, bit);
            if (!use) continue;
            const int v = bitAt(by, bit);
            cur = (uint8_t)((cur << 1) | (uint8_t)v);
            curBits++;
            if (curBits == 8) {
                out.push_back(cur);
                cur = 0;
                curBits = 0;
            }
        }
    }

    if (curBits != 0) {
        cur <<= (8 - curBits);
        out.push_back(cur);
    }

    return out;
}


static std::vector<uint8_t> applyBitmaskCharsBitstream(const std::vector<uint8_t>& src,
    const std::vector<std::vector<uint8_t>>& charMasks,
    bool byteCompletion) {

    if (src.empty() || charMasks.empty()) return src;

    auto bitAt = [](uint8_t by, int bit) -> int { // bit 0..7 means MSB..LSB
        return (by >> (7 - bit)) & 1;
    };

    std::vector<int> bits;
    bits.reserve(charMasks.size() * 8);

    for (const auto& maskBytes : charMasks) {
        std::vector<int> cbits;
        const size_t n = std::min(src.size(), maskBytes.size());
        for (size_t i = 0; i < n; ++i) {
            const uint8_t by = src[i];
            const uint8_t m = maskBytes[i];
            for (int bit = 0; bit < 8; ++bit) {
                if (!bitAt(m, bit)) continue;
                cbits.push_back(bitAt(by, bit));
            }
        }

        if (byteCompletion) {
            const int mod = (int)(cbits.size() % 8);
            if (mod != 0) {
                const int pad = 8 - mod;
                // completion is leading zeros for this character result
                cbits.insert(cbits.begin(), pad, 0);
            }
        }

        bits.insert(bits.end(), cbits.begin(), cbits.end());
    }

    std::vector<uint8_t> out;
    uint8_t cur = 0;
    int curBits = 0;
    for (int b : bits) {
        cur = (uint8_t)((cur << 1) | (uint8_t)(b & 1));
        curBits++;
        if (curBits == 8) {
            out.push_back(cur);
            cur = 0;
            curBits = 0;
        }
    }
    if (curBits != 0) {
        cur <<= (8 - curBits);
        out.push_back(cur);
    }
    return out;
}

static uint8_t extractMaskedBitsToLsb(const std::vector<uint8_t>& b, const std::vector<uint8_t>& maskBytes) {
    auto bitAt = [](uint8_t by, int bit) -> int { // bit 7..0
        return (by >> (7 - bit)) & 1;
        };

    uint8_t out = 0;
    int outBits = 0;

    const size_t n = std::min(b.size(), maskBytes.size());
    for (size_t i = 0; i < n; ++i) {
        const uint8_t by = b[i];
        const uint8_t m = maskBytes[i];
        for (int bit = 0; bit < 8; ++bit) {
            if (!bitAt(m, bit)) continue;
            out = (uint8_t)((out << 1) | (uint8_t)bitAt(by, bit));
            ++outBits;
            if (outBits == 8) return out;
        }
    }

    // Important: do NOT left-pad to MSB; keep packed bits at LSB (nibbles become 0..15).
    return out;
}

static std::vector<uint8_t> applyBitmaskPerCharText(const std::vector<uint8_t>& b,
    const std::vector<std::vector<uint8_t>>& charMasks) {
    if (b.empty() || charMasks.empty()) return b;

    std::vector<uint8_t> out;
    out.reserve(charMasks.size());
    for (const auto& cm : charMasks) {
        out.push_back(extractMaskedBitsToLsb(b, cm));
    }
    return out;
}


// ------------------------------------------------------------
// Unit extraction (bit-granular for text)
// ------------------------------------------------------------
static std::vector<uint32_t> extractUnitsMSB(const std::vector<uint8_t>& b, int srcUnitBits, int dstUnits) {
    if (b.empty() || srcUnitBits <= 0) return {};

    const int totalBits = (int)b.size() * 8;
    int maxUnits = totalBits / srcUnitBits;
    if (dstUnits > 0) maxUnits = std::min(maxUnits, dstUnits);

    auto getBit = [&](int bitIndex) -> int {
        const int byteIndex = bitIndex / 8;
        const int bitInByte = bitIndex % 8; // MSB first
        const uint8_t by = b[(size_t)byteIndex];
        return (by >> (7 - bitInByte)) & 1;
    };

    std::vector<uint32_t> units;
    units.reserve((size_t)maxUnits);

    int bitPos = 0;
    for (int u = 0; u < maxUnits; ++u) {
        uint32_t v = 0;
        for (int k = 0; k < srcUnitBits; ++k) {
            v = (v << 1) | (uint32_t)getBit(bitPos++);
        }
        units.push_back(v);
    }

    return units;
}

// ------------------------------------------------------------
// Transform pipeline (swap-skip-order)
// ------------------------------------------------------------
static std::vector<std::string> splitSemiList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ';')) {
        tok = trim(tok);
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

static void applyBytePick(std::vector<uint8_t>& b, const std::string& spec) {
    std::string s = trim(spec);
    if (s.empty()) return;

    auto parseIdx = [&](const std::string& t, int& out) -> bool {
        std::string x = trim(t);
        if (x.empty()) return false;
        char* endp = nullptr;
        long v = std::strtol(x.c_str(), &endp, 10);
        if (endp == x.c_str() || *endp != '\0') return false;
        out = (int)v;
        return true;
        };

    std::vector<uint8_t> out;
    out.reserve(b.size());

    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        tok = trim(tok);
        if (tok.empty()) continue;

        // range a-b
        size_t dash = tok.find('-');
        if (dash != std::string::npos) {
            int a = 0, c = 0;
            if (!parseIdx(tok.substr(0, dash), a)) continue;
            if (!parseIdx(tok.substr(dash + 1), c)) continue;

            if (a <= c) {
                for (int i = a; i <= c; ++i) {
                    if (i >= 0 && (size_t)i < b.size()) out.push_back(b[(size_t)i]);
                }
            }
            else {
                for (int i = a; i >= c; --i) {
                    if (i >= 0 && (size_t)i < b.size()) out.push_back(b[(size_t)i]);
                }
            }
            continue;
        }

        // single index
        int i = 0;
        if (!parseIdx(tok, i)) continue;
        if (i >= 0 && (size_t)i < b.size()) out.push_back(b[(size_t)i]);
    }

    b.swap(out);
}


static std::vector<uint8_t> buildTransformedBytes(const GameDef& def, const Elt& e, const uint8_t* p) {
    if (e.size <= 0 || !p) return {};
    std::vector<uint8_t> b(p, p + e.size);

    std::vector<std::string> order;
    if (!trim(e.swapSkipOrder).empty()) order = splitSemiList(e.swapSkipOrder);
    if (order.empty()) {
        order = {
            "byte-pick",
            "byte-skip",
            "endianness",
            "byte-trim",
            "byte-trunc",
            "byte-swap",
            "nibble-skip",
            "nibble-trim",
            "bit-swap",
            "bitmask"
        };
    }

    for (const auto& step : order) {
        if (ieq(step, "byte-pick")) {
            applyBytePick(b, e.bytePick);
        }else if (ieq(step, "endianness")) {
            applyEndianness(b, e.endian);
        } else if (ieq(step, "byte-skip")) {
            applyByteSkip(b, e.byteSkip);
        } else if (ieq(step, "byte-trim")) {
            uint8_t sent = 0;
            if (parseHexByte0x(e.byteTrim, sent)) {
                applyByteTrimSentinel(b, sent);
            } else if (!trim(e.byteTrim).empty()) {
                const int n = std::max(0, std::atoi(trim(e.byteTrim).c_str()));
                if (n >= (int)b.size()) b.clear();
                else b.erase(b.begin(), b.begin() + n);
            }
        } else if (ieq(step, "byte-trunc")) {
            uint8_t sent = 0;
            if (parseHexByte0x(e.byteTrunc, sent)) {
                applyByteTruncSentinel(b, sent);
            } else if (!trim(e.byteTrunc).empty()) {
                const int n = std::max(0, std::atoi(trim(e.byteTrunc).c_str()));
                if (n > 0 && (size_t)n < b.size()) b.resize((size_t)n);
            }
        } else if (ieq(step, "byte-swap")) {
            applyChunkReverse(b, e.byteSwap);
        } else if (ieq(step, "nibble-skip")) {
            applyNibbleSkip(b, e.nibbleSkip);
        } else if (ieq(step, "nibble-trim")) {
            uint8_t nib = 0;
            if (parseHexNibble0x(e.nibbleTrim, nib)) applyNibbleTrim(b, nib);
        } else if (ieq(step, "bit-swap")) {
            if (e.bitSwap) applyBitSwap(b);
        }
        else if (ieq(step, "bitmask")) {
            if (!e.bitmaskId.empty()) {
                auto it = Utils::findIdentifier(def.bitmasks, e.bitmaskId);
                if (it != def.bitmasks.end()) {
                    const BitmaskDef& bm = it->second;

                    // GreatStone-style: <character> masks describe per-output-character
                    // extraction for text and per-output-digit/byte extraction for ints.
                    if (!bm.charMasks.empty()) {
                        b = applyBitmaskCharsBitstream(b, bm.charMasks, bm.byteCompletion);
                    }
                    else if (!bm.mergedMask.empty()) {
                        b = applyBitmaskPack(b, bm.mergedMask);
                    }
                    // Fallback: old pack behavior (useful for other defs you already handle)
                    else if (!bm.mergedMask.empty()) {
                        b = applyBitmaskPack(b, bm.mergedMask);
                    }
                }
            }
        }

    }

    return b;
}

// ------------------------------------------------------------
// Integer decode (hex / BCD)
// ------------------------------------------------------------
static std::string hexBytes0x(const std::vector<uint8_t>& b) {
    static const char* H = "0123456789ABCDEF";
    std::string s;
    s.reserve(2 + b.size() * 2);
    s += "0x";
    for (uint8_t x : b) {
        s += H[(x >> 4) & 0xF];
        s += H[(x >> 0) & 0xF];
    }
    return s;
}

static bool isValidBcdAllNibbles(const std::vector<uint8_t>& b) {
    for (uint8_t by : b) {
        const int hi = (by >> 4) & 0xF;
        const int lo = by & 0xF;
        if (hi > 9 || lo > 9) return false;
    }
    return true;
}

static int64_t bcdMsbToIntAllNibblesStrict(const std::vector<uint8_t>& b) {
    int64_t r = 0;
    for (uint8_t by : b) {
        const int hi = (by >> 4) & 0xF;
        const int lo = by & 0xF;
        r = r * 10 + hi;
        r = r * 10 + lo;
    }
    return r;
}

static int64_t rawBytesToInt(const std::vector<uint8_t>& b) {
    uint64_t raw = 0;
    for (uint8_t x : b) raw = (raw << 8) | (uint64_t)x;
    return (int64_t)raw;
}

static int64_t convertIntToBaseDigits(int64_t value, int base) {
    if (base <= 1) return value;
    if (value == 0) return 0;

    uint64_t n = (value < 0) ? (uint64_t)(-value) : (uint64_t)value;
    std::vector<int> digits;
    while (n != 0) {
        digits.push_back((int)(n % (uint64_t)base));
        n /= (uint64_t)base;
    }
    std::reverse(digits.begin(), digits.end());

    std::string s;
    for (int d : digits) s += std::to_string(d);

    int64_t out = std::atoll(s.c_str());
    return (value < 0) ? -out : out;
}

static Value decodeInt(const GameDef& def, const Elt& e, const uint8_t* p) {
    std::vector<uint8_t> b = buildTransformedBytes(def, e, p);
    // Official hi2txt treats an integer whose transformation removes every
    // byte (for example byte-trim removing an unused score sentinel) as zero,
    // rather than as a missing value.
    if (b.empty()) return (int64_t)0;

    const std::string dp = Utils::trim(e.decodingProfile);
    const bool profBcd = Utils::ieq(dp, "bcd");
    const bool profBcdLe = Utils::ieq(dp, "bcd-le");

    if (profBcd || profBcdLe || e.intBase == IntBaseKind::BcdBE || e.intBase == IntBaseKind::BcdLE) {

        if (!isValidBcdAllNibbles(b)) {
            throw std::runtime_error(
                "Wrong " + e.id + " value (" + hexBytes0x(b) +
                ") encoding detected: it can be due to a temporary corrupted .hi or nvram file"
            );
        }

        // bcd-le decoding-profile is expanded into byte order/nibble operations
        // during parsing; an explicit bcd-le integer base still reverses here.
        if (e.intBase == IntBaseKind::BcdLE) std::reverse(b.begin(), b.end());

        int64_t v = (int64_t)bcdMsbToIntAllNibblesStrict(b);

        return v;
    }

    int64_t raw = rawBytesToInt(b);
    if (e.numericBase > 1) {
        raw = convertIntToBaseDigits(raw, e.numericBase);
    }
    return raw;
}
// ------------------------------------------------------------
// Text decode (charset + ascii-step/offset + unit sizing)
// ------------------------------------------------------------

struct CharsetStage {
    enum Kind { BuiltinCsNumber, Map } kind = Map;
    int csNumberBase = 10;      // future-proof
    int csNumberShift = 0;      // from [..] if present; not needed for sinistar fix
    int csNumberZeroCode = 48;
    int csNumberDelta = 0;
    bool csNumberHasDelta = false;
    const Charset* map = nullptr;
};

// parses "CS_NUMBER[-48]" and "CS_NUMBER[4,1]"
static bool parseCsNumberStage(const std::string& tok, CharsetStage& st) {
    std::string s = Utils::trim(tok);
    if (!Utils::ieq(Utils::trim(s.substr(0, 9)), "CS_NUMBER")) return false;

    st.kind = CharsetStage::BuiltinCsNumber;
    st.csNumberBase = 10;
    st.csNumberShift = 0;
    st.csNumberZeroCode = 48;
    st.csNumberDelta = 0;
    st.csNumberHasDelta = false;

    auto lb = s.find('[');
    auto rb = s.find(']');
    if (lb != std::string::npos && rb != std::string::npos && rb > lb + 1) {
        std::string inner = Utils::trim(s.substr(lb + 1, rb - lb - 1));
        const size_t comma = inner.find(',');
        if (comma == std::string::npos) {
            // Legacy shorthand: value is the game-code offset from ASCII '0'.
            // Example: [-47] means raw 0x01 is digit 0.
            st.csNumberShift = std::atoi(inner.c_str());
            st.csNumberZeroCode = (int)'0' + st.csNumberShift;
        }
        else {
            st.csNumberShift = std::atoi(Utils::trim(inner.substr(0, comma)).c_str());
            st.csNumberDelta = std::atoi(Utils::trim(inner.substr(comma + 1)).c_str());
            st.csNumberHasDelta = true;
        }
    }
    return true;
}

static std::vector<CharsetStage> buildCharsetStages(const GameDef& def, const std::string& chain) {
    std::vector<CharsetStage> parsed; // left-to-right tokens
    for (const auto& tok : splitSemiList(chain)) {
        CharsetStage st;

        if (parseCsNumberStage(tok, st)) {
            parsed.push_back(st);
            continue;
        }

        auto it = Utils::findIdentifier(def.charsets, tok);
        if (it != def.charsets.end()) {
            st.kind = CharsetStage::Map;
            st.map = &it->second;
            parsed.push_back(st);
            continue;
        }
    }

    // execution order is right-to-left
    std::reverse(parsed.begin(), parsed.end());
    return parsed;
}

// apply pipeline for one unit.
// returns true if it consumed and produced output text; false means "still have a unit to keep processing".
static bool applyCharsetStagesOne(const std::vector<CharsetStage>& stages,
    uint32_t inUnit,
    uint32_t& outUnit,
    std::string& outText,
    bool& stop) {
    stop = false;
    outText.clear();
    outUnit = inUnit;

    for (const auto& st : stages) {
        if (st.kind == CharsetStage::Map && st.map) {
            auto mit = st.map->entries.find(outUnit);
            if (mit != st.map->entries.end()) {
                outText = mit->second;
                return true;
            }
            if (outUnit <= 255u && st.map->used[(uint8_t)outUnit]) {
                // empty string is valid output; NOT a terminator
                outText = st.map->dst[(uint8_t)outUnit];
                return true; // consumed
            }
            continue; // no mapping => pass-through
        }

        if (st.kind == CharsetStage::BuiltinCsNumber) {
            int digit = -1;
            if (st.csNumberHasDelta) {
                const int raw = (int)(outUnit & 0xFFu);
                const int first = (int)'0' + st.csNumberShift;
                const int step = 1 + st.csNumberDelta;
                if (step != 0 && raw >= first && ((raw - first) % step) == 0) {
                    digit = (raw - first) / step;
                }
            }
            else {
                digit = (int)(outUnit & 0xFFu) - st.csNumberZeroCode;
            }
            if (digit >= 0 && digit <= 9) {
                outText.assign(1, (char)('0' + digit));
                return true; // consumed
            }
            continue; // pass-through
        }
    }

    return false; // not consumed
}

static std::string utf8FromCodepoint(int v) {
    if (v < 0) v = 0x10000 + v;
    if (v < 0) v = 0;
    if (v > 0x10FFFF) v = 0xFFFD;

    std::string out;
    uint32_t cp = (uint32_t)v;
    if (cp <= 0x7Fu) {
        out.push_back((char)cp);
    }
    else if (cp <= 0x7FFu) {
        out.push_back((char)(0xC0u | (cp >> 6)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else if (cp <= 0xFFFFu) {
        out.push_back((char)(0xE0u | (cp >> 12)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    else {
        out.push_back((char)(0xF0u | (cp >> 18)));
        out.push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
    return out;
}


static std::string decodeText(const GameDef& def, const Elt& e, const uint8_t* p) {
    std::vector<uint8_t> b = buildTransformedBytes(def, e, p);
    if (b.empty()) return "";

    const std::vector<CharsetStage> stages =
        e.charsetId.empty() ? std::vector<CharsetStage>{} : buildCharsetStages(def, e.charsetId);

    const bool hasStages = !stages.empty();

    auto mapUnit = [&](uint32_t unit, bool& stop) -> std::string {
        stop = false;

        // If we have charset stages, do NOT apply implicit 0-terminator.
        if (hasStages) {
            uint32_t u2 = unit;
            std::string outText;
            bool consumed = applyCharsetStagesOne(stages, unit, u2, outText, stop);
            if (consumed) return outText;

            // fall back to the decoded unit itself; 16-bit text units may intentionally map to Unicode codepoints.
            int v = (int)u2;
            if (e.hasAsciiStep && e.asciiStep != 0) v = v / e.asciiStep;
            if (e.hasAsciiOffset) v += e.asciiOffset;
            return utf8FromCodepoint(v);
        }

        int v = (int)unit;
        if (e.hasAsciiStep && e.asciiStep != 0) v = v / e.asciiStep;
        if (e.hasAsciiOffset) v += e.asciiOffset;
        return utf8FromCodepoint(v);
        };
    std::string out;

    if (e.srcUnitSizeBits > 0) {
        const int totalBits = (int)b.size() * 8;
        int dstUnits = totalBits / e.srcUnitSizeBits;

        if (e.hasTextBase && (e.textBase == 32 || e.textBase == 40) && e.dstUnitCount > 0) {
            dstUnits = e.dstUnitCount;
        }

        auto units = extractUnitsMSB(b, e.srcUnitSizeBits, dstUnits);

        if (e.hasTextBase && (e.textBase == 32 || e.textBase == 40) && e.dstUnitCount > 0) {
            const int baseN = e.textBase;
            const int digits = e.dstUnitCount;

            for (auto u0 : units) {
                uint32_t u = u0;
                std::vector<uint32_t> d;
                d.reserve((size_t)digits);
                for (int i = 0; i < digits; ++i) {
                    d.push_back(u % (uint32_t)baseN);
                    u /= (uint32_t)baseN;
                }
                std::reverse(d.begin(), d.end());

                for (auto digit : d) {
                    bool stop = false;
                    std::string s = mapUnit(digit, stop);
                    if (stop) return out;
                    out += s;
                }
            }
            return out;
        }

        for (auto u : units) {
            bool stop = false;
            std::string s = mapUnit(u, stop);
            if (stop) break;
            out += s;
        }

        // Some legacy dumps truncate the final multi-byte text unit while
        // providing one-byte charset entries for exactly that case.
        const int consumedBits = (int)units.size() * e.srcUnitSizeBits;
        const int remainingBits = totalBits - consumedBits;
        if (hasStages && remainingBits > 0 && remainingBits <= 32 &&
            (remainingBits % 8) == 0) {
            uint32_t partial = 0;
            for (int bitIndex = consumedBits; bitIndex < totalBits; ++bitIndex) {
                const uint8_t byte = b[(size_t)(bitIndex / 8)];
                partial = (partial << 1) |
                    (uint32_t)((byte >> (7 - (bitIndex % 8))) & 1);
            }
            bool stop = false;
            std::string s = mapUnit(partial, stop);
            if (!stop) out += s;
        }
        return out;
    }

    // Some definitions describe word-aligned printable ASCII without an
    // explicit byte-skip (for example 00 41 00 42 00 43 for "ABC").
    // Detect only that unambiguous shape: zero bytes in one complete lane
    // and printable ASCII in the other. A general zero-byte removal would
    // corrupt charsets where 0x00 is meaningful and NUL-terminated fields.
    if (!hasStages && !e.hasAsciiStep && !e.hasAsciiOffset &&
        b.size() >= 4 && (b.size() % 2) == 0) {
        for (size_t paddingLane = 0; paddingLane < 2; ++paddingLane) {
            bool wordAlignedAscii = true;
            for (size_t i = 0; i < b.size(); ++i) {
                if ((i % 2) == paddingLane) {
                    if (b[i] != 0) {
                        wordAlignedAscii = false;
                        break;
                    }
                }
                else if (b[i] < 0x20 || b[i] > 0x7E) {
                    wordAlignedAscii = false;
                    break;
                }
            }
            if (wordAlignedAscii) {
                for (size_t i = 1 - paddingLane; i < b.size(); i += 2) {
                    bool stop = false;
                    out += mapUnit((uint32_t)b[i], stop);
                    if (stop) break;
                }
                return out;
            }
        }
    }

    for (size_t i = 0; i < b.size(); ++i) {
        bool stop = false;
        std::string s = mapUnit((uint32_t)b[i], stop);
        if (stop) break;
        out += s;
    }
    return out;
}

static Value decodeRaw(const GameDef& def, const Elt& e, const uint8_t* p) {
    return buildTransformedBytes(def, e, p);
}

static Value decode(const GameDef& def, const Elt& e, const uint8_t* p) {
    if (e.size <= 0) return {};
    if (ieq(e.type, "text")) return decodeText(def, e, p);
    if (ieq(e.type, "int"))  return decodeInt(def, e, p);
    if (ieq(e.type, "raw"))  return decodeRaw(def, e, p);
    return {};
}


// ------------------------------------------------------------
// Slice helper
// ------------------------------------------------------------
static bool canSlice(const std::vector<uint8_t>& raw, size_t off, size_t sz) {
    return (off <= raw.size()) && (sz <= raw.size() - off);
}

// ------------------------------------------------------------
// Table-index-format support
// ------------------------------------------------------------
static int applyTableIndexFormat(const GameDef& def, int base, const std::string& fmtIdOrExpr) {
    std::string s = Utils::trim(fmtIdOrExpr);
    if (s.empty()) return base;

    // If it's a known <format id="...">, reuse your existing Formatter path.
    // This is ideal because klax uses <format id="*2"> etc.
    auto it = Utils::findIdentifier(def.formats, s);
    if (it != def.formats.end()) {
        Value v = (int64_t)base;
        // row context irrelevant here; pass empty row and loopIndex=-1
        static std::unordered_map<std::string, Value> dummyRow;
        v = Formatter::apply(def.formats, s, dummyRow, v, -1);
        return (int)Utils::valueToInt(v);
    }

    // Otherwise treat it as an inline expression chain like "*2+1" or "*2" or "-1".
    // Parse left-to-right.
    int x = base;
    size_t i = 0;

    auto parseSignedInt = [&](int& out) -> bool {
        if (i >= s.size()) return false;
        int sign = 1;
        if (s[i] == '+') { sign = 1; ++i; }
        else if (s[i] == '-') { sign = -1; ++i; }
        if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return false;
        int n = 0;
        while (i < s.size() && std::isdigit((unsigned char)s[i])) {
            n = n * 10 + (s[i] - '0');
            ++i;
        }
        out = sign * n;
        return true;
        };

    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i >= s.size()) break;

        char op = s[i];

        if (op == ';' || op == '|') {
            ++i;
            continue;
        }

        if (op == '*' || op == 'x' || op == 'X') {
            ++i;
            int n = 0;
            // allow "*2"/"x2"/"X2" only
            if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return x;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) {
                n = n * 10 + (s[i] - '0');
                ++i;
            }
            x *= n;
            continue;
        }

        if (op == '/') {
            ++i;
            int n = 0;
            if (i >= s.size() || !std::isdigit((unsigned char)s[i])) return x;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) {
                n = n * 10 + (s[i] - '0');
                ++i;
            }
            if (n != 0) x /= n;
            continue;
        }

        if (op == '+' || op == '-') {
            int dn = 0;
            if (!parseSignedInt(dn)) return x;
            x += dn;
            continue;
        }

        // If expression starts with digits, treat as absolute override.
        if (std::isdigit((unsigned char)op)) {
            int n = 0;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) {
                n = n * 10 + (s[i] - '0');
                ++i;
            }
            x = n;
            continue;
        }

        // Unknown token -> stop, keep current.
        break;
    }

    return x;
}

static int currentMaxRow(const std::vector<std::unordered_map<std::string, Value>>& rows) {
    return rows.empty() ? 0 : (int)rows.size() - 1;
}

static int resolveRowForElt(const GameDef& def,
                            const std::vector<std::unordered_map<std::string, Value>>& rows,
                            const Elt& e,
                            int loopStartIndex,
                            int loopI,
                            int loopCount,
                            int loopStep,
                            bool inLoop) {
    const int loopIdx = loopStartIndex + loopI * loopStep;
    const int loopRevIdx = loopStartIndex + (loopCount - 1 - loopI) * loopStep;

    int row = 0;

    switch (e.tableIndexKind) {
        case TableIndexKind::Fixed: row = e.tableIndexFixed; break;
        case TableIndexKind::LoopIndex: row = loopIdx; break;
        case TableIndexKind::LoopReverseIndex: row = loopRevIdx; break;
        case TableIndexKind::Last: row = std::max(0, currentMaxRow(rows)); break;
        case TableIndexKind::Itself: row = inLoop ? loopIdx : 0; break;

        case TableIndexKind::IndexFromValue: {
            const int ctx = inLoop ? loopIdx : 0;
            if (ctx >= 0 && (size_t)ctx < rows.size()) {
                auto it = Utils::findIdentifier(rows[(size_t)ctx], e.tableIndexCol);
                row = (it != rows[(size_t)ctx].end()) ? (int)Utils::valueToInt(it->second) : loopIdx;
            } else row = loopIdx;
        } break;

        case TableIndexKind::ValueFromIndex: {
            const int target = inLoop ? loopIdx : 0;
            int found = -1;
            for (size_t r = 0; r < rows.size(); ++r) {
                auto it = Utils::findIdentifier(rows[r], e.tableIndexCol);
                if (it != rows[r].end() && (int)Utils::valueToInt(it->second) == target) { found = (int)r; break; }
            }
            row = (found >= 0) ? found : target;
        } break;

        case TableIndexKind::None:
        default:
            row = inLoop ? loopIdx : 0;
            break;
    }

    if (!trim(e.tableIndexFormat).empty()) row = applyTableIndexFormat(def, row, e.tableIndexFormat);
    return row;
}

// ------------------------------------------------------------
// Row extraction
// ------------------------------------------------------------
std::vector<std::unordered_map<std::string, Value>> Processor::extractRows(const std::vector<uint8_t>& raw,
                                                                           const Structure& s,
                                                                           const GameDef& def,
                                                                           const TraceSink* trace) {
    std::vector<std::unordered_map<std::string, Value>> rows;
    rows.reserve(64);

    int lastRowTouched = 0;

    auto ensureRow = [&](int r) -> std::unordered_map<std::string, Value>& {
        if (r < 0) r = 0;
        if (rows.size() <= (size_t)r) rows.resize((size_t)r + 1);
        return rows[(size_t)r];
    };

    auto resolveRowForElt2 =
        [&](const Elt& e,
            int loopStartIndex, int loopI, int loopCount, int loopStep,
            bool inLoop) -> int
        {
            const int loopIdx = loopStartIndex + loopI * loopStep;
            const int loopRevIdx = loopStartIndex + (loopCount - 1 - loopI) * loopStep;

            int row = 0;
            switch (e.tableIndexKind) {
                case TableIndexKind::Fixed:
                row = e.tableIndexFixed;
                break;

                case TableIndexKind::LoopIndex:
                row = loopIdx;
                break;

                case TableIndexKind::LoopReverseIndex:
                row = loopRevIdx;
                break;

                case TableIndexKind::Last:
                row = lastRowTouched;
                if (row < 0) row = 0;
                break;

                case TableIndexKind::IndexFromValue: {
                    const int ctx = inLoop ? loopIdx : 0;
                    if (ctx >= 0 && (size_t)ctx < rows.size()) {
                        auto it = Utils::findIdentifier(rows[(size_t)ctx], e.tableIndexCol);
                        row = (it != rows[(size_t)ctx].end())
                            ? (int)Utils::valueToInt(it->second)
                            : loopIdx;
                    }
                    else {
                        row = loopIdx;
                    }
                } break;

                case TableIndexKind::ValueFromIndex: {
                    const int target = inLoop ? loopIdx : 0;
                    int found = -1;
                    for (size_t r = 0; r < rows.size(); ++r) {
                        auto it = Utils::findIdentifier(rows[r], e.tableIndexCol);
                        if (it != rows[r].end() &&
                            (int)Utils::valueToInt(it->second) == target) {
                            found = (int)r;
                            break;
                        }
                    }
                    row = (found >= 0) ? found : target;
                } break;

                case TableIndexKind::Itself:
                case TableIndexKind::None:
                default:
                row = inLoop ? loopIdx : 0;
                break;
            }

            if (!Utils::trim(e.tableIndexFormat).empty())
                row = applyTableIndexFormat(def, row, e.tableIndexFormat);

            return row;
        };


    auto decodeEltAt = [&](const Elt& el,
        size_t p,
        int loopStartIndex,
        int loopI,
        int loopCount,
        int loopStep,
        bool inLoop)
        {
            if (el.size <= 0) return;
            if (p >= raw.size()) return;

            const size_t need = (size_t)el.size;
            const size_t avail = raw.size() - p;

            bool allowShort =
                (avail > 0) &&
                (avail < need) &&
                (ieq(el.type, "text") || ieq(el.type, "raw") || ieq(el.type, "int"));

            const uint8_t* srcPtr = raw.data() + p;
            Elt shortEl;
            const Elt* decodeEl = &el;

            if (avail < need) {
                if (!allowShort) return;

                shortEl = el;
                shortEl.size = (int)avail;
                decodeEl = &shortEl;
            }

            const int loopIdx = loopStartIndex + loopI * loopStep;

            Value decoded = decode(def, *decodeEl, srcPtr);

            int row = 0;
            if (el.tableIndexKind == TableIndexKind::Itself) {
                int idx = (int)Utils::valueToInt(decoded);
                if (!Utils::trim(el.tableIndexFormat).empty())
                    idx = applyTableIndexFormat(def, idx, el.tableIndexFormat);
                if (idx < 0 || idx > 100000) return;
                row = idx;
            }
            else {
                row = resolveRowForElt2(el, loopStartIndex, loopI, loopCount, loopStep, inLoop);
                if (row < 0 || row > 100000) return;
            }

            Value v = decoded;
            if (!Utils::trim(el.format).empty())
                v = Formatter::apply(def.formats, el.format, ensureRow(row), v, inLoop ? loopIdx : -1);

            auto& destination = ensureRow(row);
            auto existingValue = Utils::findIdentifier(destination, el.id);
            if (existingValue == destination.end()) destination.emplace(el.id, std::move(v));
            else existingValue->second = std::move(v);
            lastRowTouched = row;

            if (trace) {
                const size_t bytesRead = std::min(raw.size(), p + (size_t)std::max(0, decodeEl->size));
                if (inLoop) {
                    trace->line("TRACE: structure loop elt (index=" + std::to_string(loopI) + "): " +
                                el.id + " (" + std::to_string(bytesRead) + " bytes read on " +
                                std::to_string(raw.size()) + ")");
                }
                else {
                    trace->line("TRACE: structure simple elt: " + el.id + " (" +
                                std::to_string(bytesRead) + " bytes read on " +
                                std::to_string(raw.size()) + ")");
                }
            }
        };



    size_t cursor = 0;

    for (const auto& it : s.items) {
        if (it.kind == StructureItem::Kind::Elt) {
            const Elt& el = it.elt;
            const size_t p = (el.offset >= 0) ? (size_t)el.offset : cursor;

            decodeEltAt(el, p, 0, 0, 1, 1, false);
            cursor = std::max(cursor, p + (size_t)std::max(0, el.size));
            continue;
        }

        const Loop& lp = it.loop;
        const int count = std::max(0, lp.count);
        if (count == 0) continue;

        const int loopStart = lp.hasStart ? lp.startIndex : 0;
        const int loopStep  = lp.hasStep ? lp.step : 1;

        int iterBytesFull = 0;
        for (const auto& e : lp.elts) iterBytesFull += std::max(0, e.size);

        int lastIterLimit = iterBytesFull - std::max(0, lp.skipLastBytes);
        if (lastIterLimit < 0) lastIterLimit = 0;

        size_t loopCursor = cursor;

        for (int i = 0; i < count; ++i) {
            const bool isLast = (i == count - 1);
            const int byteLimit = isLast ? lastIterLimit : iterBytesFull;
            const int skipPrefix = (i == 0) ? std::max(0, lp.skipFirstBytes) : 0;
            const int fileBytesThisIter = std::max(0, byteLimit - skipPrefix);

            size_t iterBase = loopCursor;
            int consumed = 0;

            for (const auto& el : lp.elts) {
                const int esz = std::max(0, el.size);
                if (esz <= 0) continue;
                const int logicalStart = consumed;
                const int logicalEnd = consumed + esz;

                if (logicalEnd <= skipPrefix) {
                    consumed += esz;
                    continue;
                }

                if (logicalStart >= byteLimit) {
                    break;
                }

                if (logicalEnd > byteLimit) {
                    const bool partialAllowed =
                        isLast &&
                        logicalStart < byteLimit &&
                        (ieq(el.type, "text") || ieq(el.type, "raw") || ieq(el.type, "int"));

                    if (partialAllowed) {
                        const int readStart = std::max(logicalStart, skipPrefix);
                        const int readLen = byteLimit - readStart;
                        if (readLen > 0) {
                            Elt partialEl = el;
                            partialEl.size = readLen;
                            const size_t p = (el.offset >= 0) ? (size_t)el.offset : (iterBase + (size_t)(readStart - skipPrefix));
                            decodeEltAt(partialEl, p, loopStart, i, count, loopStep, true);
                        }
                    }
                    break;
                }

                const int readStart = std::max(logicalStart, skipPrefix);
                const int readLen = logicalEnd - readStart;
                if (readLen > 0) {
                    Elt readEl = el;
                    readEl.size = readLen;
                    const size_t p = (el.offset >= 0) ? (size_t)el.offset : (iterBase + (size_t)(readStart - skipPrefix));
                    decodeEltAt(readEl, p, loopStart, i, count, loopStep, true);
                }

                consumed += esz;
            }

            loopCursor = iterBase + (size_t)fileBytesThisIter;
            if (loopCursor > raw.size()) loopCursor = raw.size();
        }

        cursor = std::max(cursor, loopCursor);
    }

    if (rows.empty()) rows.resize(1);
    return rows;
}

} // namespace openhi2txt
