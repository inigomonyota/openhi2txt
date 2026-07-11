#include "io/Utils.h"
#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <algorithm>

namespace openhi2txt::Utils {

std::string trim(std::string s) {
    auto issp = [](unsigned char ch) { return !!std::isspace(ch); };
    while (!s.empty() && issp((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && issp((unsigned char)s.back())) s.pop_back();
    return s;
}

bool ieq(std::string a, std::string b) {
    a = trim(std::move(a));
    b = trim(std::move(b));
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    }
    return true;
}

std::string basenameOf(const std::string& path) {
    size_t p1 = path.find_last_of('/');
    size_t p2 = path.find_last_of('\\');
    size_t p = std::string::npos;
    if (p1 != std::string::npos) p = p1;
    if (p2 != std::string::npos) p = (p == std::string::npos) ? p2 : std::max(p, p2);
    if (p == std::string::npos) return path;
    return path.substr(p + 1);
}

int slashCount(const std::string& s) {
    int n = 0;
    for (char ch : s) if (ch == '/' || ch == '\\') ++n;
    return n;
}

int parseNum(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return -1;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        return (int)std::strtol(t.c_str() + 2, nullptr, 16);
    return std::atoi(t.c_str());
}

bool parseBool(const std::string& s, bool defaultValue) {
    std::string t = trim(s);
    if (t.empty()) return defaultValue;
    if (ieq(t, "1") || ieq(t, "yes") || ieq(t, "true")) return true;
    if (ieq(t, "0") || ieq(t, "no") || ieq(t, "false")) return false;
    // fall back: numeric non-zero
    if (looksNumeric(t)) return std::atoll(t.c_str()) != 0;
    return defaultValue;
}

int64_t parseInt64Auto(const std::string& s, bool* ok) {
    std::string t = trim(s);
    if (t.empty()) { if (ok) *ok = false; return 0; }
    int base = 10;
    const char* p = t.c_str();
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { base = 16; p += 2; }
    char* endp = nullptr;
    long long v = std::strtoll(p, &endp, base);
    if (ok) *ok = (endp && *endp == '\0');
    return (int64_t)v;
}

bool parseHexByte0x(const std::string& s, uint8_t& out) {
    std::string t = trim(s);
    if (t.size() == 4 && (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) &&
        std::isxdigit((unsigned char)t[2]) && std::isxdigit((unsigned char)t[3])) {
        out = (uint8_t)std::strtoul(t.c_str() + 2, nullptr, 16);
        return true;
    }
    return false;
}

bool parseHexNibble0x(const std::string& s, uint8_t& outNib) {
    std::string t = trim(s);
    if (t.size() == 3 && (t.rfind("0x", 0) == 0 || t.rfind("0X", 0) == 0) &&
        std::isxdigit((unsigned char)t[2])) {
        outNib = (uint8_t)std::strtoul(t.c_str() + 2, nullptr, 16);
        outNib &= 0xF;
        return true;
    }
    return false;
}

std::string trimLeftBySet(const std::string& s, const std::string& chars) {
    size_t i = 0;
    while (i < s.size() && chars.find(s[i]) != std::string::npos) ++i;
    return s.substr(i);
}

std::string trimRightBySet(const std::string& s, const std::string& chars) {
    if (s.empty()) return s;
    size_t j = s.size();
    while (j > 0 && chars.find(s[j - 1]) != std::string::npos) --j;
    return s.substr(0, j);
}

bool looksNumeric(const std::string& s) {
    std::string t = trim(s);
    if (t.empty()) return false;
    size_t i = 0;
    if (t[0] == '+' || t[0] == '-') i = 1;
    bool any = false;
    for (; i < t.size(); ++i) {
        if (t[i] < '0' || t[i] > '9') return false;
        any = true;
    }
    return any;
}

std::string valueToString(const Value& v) {
    if (auto* s = std::get_if<std::string>(&v)) return *s;
    if (auto* n = std::get_if<int64_t>(&v)) return std::to_string(*n);

    if (auto* b = std::get_if<RawBytes>(&v)) {
        static const char* H = "0123456789ABCDEF";
        std::string out;
        out.reserve(b->size() * 2);
        for (uint8_t x : *b) {
            out.push_back(H[(x >> 4) & 0xF]);
            out.push_back(H[x & 0xF]);
        }
        return out;
    }
    return "";
}

int64_t valueToInt(const Value& v) {
    if (auto* n = std::get_if<int64_t>(&v)) return *n;
    if (auto* s = std::get_if<std::string>(&v)) return std::atoll(s->c_str());
    return 0;
}

bool readFileBytes(const std::filesystem::path& p, std::vector<uint8_t>& out) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

static bool looksLikeEntityAt(const std::string& s, size_t i, size_t& endSemi) {
    if (i >= s.size() || s[i] != '&') return false;
    size_t j = i + 1;
    if (j >= s.size()) return false;
    size_t semi = s.find(';', j);
    if (semi == std::string::npos) return false;
    if (semi == j) return false;
    for (size_t k = j; k < semi; ++k) {
        unsigned char ch = (unsigned char)s[k];
        if (!(std::isalnum(ch) || ch == '_' || ch == '-')) return false;
    }
    endSemi = semi;
    return true;
}

static const char* legacyEntityUtf8(const std::string& name) {
    if (name == "mens-symbol" || name == "mans-symbol") return "\xE2\x99\x82";
    if (name == "womens-symbol") return "\xE2\x99\x80";
    if (name == "headset") return "\xE2\x98\x8A";
    if (name == "slash-in-square") return "\xE2\x96\xA7";
    if (name == "antislash-in-square") return "\xE2\x96\xA8";
    if (name == "h-lines-in-square") return "\xE2\x96\xA4";
    return nullptr;
}

static bool decodeUtf8At(const std::string& s, size_t i, uint32_t& cp, size_t& len) {
    const unsigned char c0 = (unsigned char)s[i];
    if (c0 < 0x80) {
        cp = c0;
        len = 1;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0 && i + 1 < s.size()) {
        const unsigned char c1 = (unsigned char)s[i + 1];
        if ((c1 & 0xC0) != 0x80) return false;
        cp = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
        len = 2;
        return cp >= 0x80;
    }
    if ((c0 & 0xF0) == 0xE0 && i + 2 < s.size()) {
        const unsigned char c1 = (unsigned char)s[i + 1];
        const unsigned char c2 = (unsigned char)s[i + 2];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return false;
        cp = ((uint32_t)(c0 & 0x0F) << 12) |
             ((uint32_t)(c1 & 0x3F) << 6) |
             (uint32_t)(c2 & 0x3F);
        len = 3;
        return cp >= 0x800;
    }
    if ((c0 & 0xF8) == 0xF0 && i + 3 < s.size()) {
        const unsigned char c1 = (unsigned char)s[i + 1];
        const unsigned char c2 = (unsigned char)s[i + 2];
        const unsigned char c3 = (unsigned char)s[i + 3];
        if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return false;
        cp = ((uint32_t)(c0 & 0x07) << 18) |
             ((uint32_t)(c1 & 0x3F) << 12) |
             ((uint32_t)(c2 & 0x3F) << 6) |
             (uint32_t)(c3 & 0x3F);
        len = 4;
        return cp >= 0x10000 && cp <= 0x10FFFF;
    }
    return false;
}

void xmlEscapePrintPreserveEntities(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char ch = s[i];
        if (ch == '&') {
            size_t semi = 0;
            if (looksLikeEntityAt(s, i, semi)) {
                std::string entityName = s.substr(i + 1, semi - i - 1);
                if (const char* replacement = legacyEntityUtf8(entityName)) {
                    std::printf("%s", replacement);
                    i = semi;
                    continue;
                }
                std::printf("%.*s", (int)(semi - i + 1), s.c_str() + (int)i);
                i = semi;
                continue;
            }
            std::printf("&amp;");
        }
        else if (ch == '<') std::printf("&lt;");
        else if (ch == '>') std::printf("&gt;");
        else if (ch == '"') std::printf("&quot;");
        else if (ch == '\'') std::printf("&apos;");
        else if ((unsigned char)ch >= 0x80) {
            uint32_t cp = 0;
            size_t len = 0;
            if (decodeUtf8At(s, i, cp, len)) {
                std::printf("&#%u;", (unsigned)cp);
                i += len - 1;
            }
            else {
                std::printf("&#%u;", (unsigned char)ch);
            }
        }
        else std::printf("%c", ch);
    }
}

} // namespace openhi2txt::Utils
