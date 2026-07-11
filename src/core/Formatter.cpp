#include "core/Formatter.h"
#include "io/Utils.h"

#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace openhi2txt {

using namespace Utils;

static std::string toLower(std::string s) {
    for (char& ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);
        // Only transform if it's a standard ASCII character
        if (uc <= 127) {
            ch = static_cast<char>(std::tolower(uc));
        }
    }
    return s;
}

static std::string toUpper(std::string s) {
    for (char& ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);
        // Only transform if it's a standard ASCII character
        if (uc <= 127) {
            ch = static_cast<char>(std::toupper(uc));
        }
    }
    return s;
}

static std::string toCap(std::string s) {
    bool atStart = true;
    for (char& ch : s) {
        unsigned char uc = static_cast<unsigned char>(ch);

        // Check for whitespace to reset the "start of word" flag
        // std::isspace is safe here because UTF-8 multi-bytes are never 0x20, 0x09, etc.
        if (uc <= 127 && std::isspace(uc)) {
            atStart = true;
            continue;
        }

        if (atStart) {
            if (uc <= 127) ch = static_cast<char>(std::toupper(uc));
            atStart = false;
        }
        else {
            if (uc <= 127) ch = static_cast<char>(std::tolower(uc));
        }
    }
    return s;
}

static std::string doTrim(std::string s, const TrimOp& t) {
    if (t.dir == TrimDir::None) return s;
    std::string chars = t.chars.empty() ? " " : t.chars;
    if (t.dir == TrimDir::Left || t.dir == TrimDir::Both) s = trimLeftBySet(s, chars);
    if (t.dir == TrimDir::Right || t.dir == TrimDir::Both) s = trimRightBySet(s, chars);
    return s;
}

static std::string doPad(std::string s, const PadOp& p) {
    if (!p.enabled || p.max <= 0) return s;
    std::string pad = p.padChar.empty() ? "0" : p.padChar;
    while ((int)s.size() < p.max) {
        if (p.dir == TrimDir::Right) s += pad;
        else s = pad + s;
    }
    if ((int)s.size() > p.max) {
        if (p.dir == TrimDir::Right) s.resize(p.max);
        else s = s.substr(s.size() - p.max);
    }
    return s;
}

static std::string doReplace(std::string s, const ReplaceOp& r) {
    if (!r.enabled || r.src.empty()) return s;
    if (!r.all) {
        size_t p = s.find(r.src);
        if (p != std::string::npos) s.replace(p, r.src.size(), r.dst);
        return s;
    }
    size_t p = 0;
    while ((p = s.find(r.src, p)) != std::string::npos) {
        s.replace(p, r.src.size(), r.dst);
        p += r.dst.size();
    }
    return s;
}

static std::string formatNumberWithFormatter(double x, const std::string& fmt) {
    if (fmt.empty()) {
        std::ostringstream oss;
        oss.setf(std::ios::fixed);
        oss.precision(12);
        oss << x;
        std::string s = oss.str();
        // trim trailing zeros/dot
        while (s.size() > 1 && s.find('.') != std::string::npos && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
        return s;
    }

    char buf[256];
    // printf-style subset (examples in spec use %.2fsec)
    bool integerFormatter = false;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%') continue;
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
            ++i;
            continue;
        }

        size_t j = i + 1;
        while (j < fmt.size() && std::string("-+ #0").find(fmt[j]) != std::string::npos) ++j;
        while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) ++j;
        if (j < fmt.size() && fmt[j] == '.') {
            ++j;
            while (j < fmt.size() && std::isdigit((unsigned char)fmt[j])) ++j;
        }
        while (j < fmt.size() && std::string("hljztL").find(fmt[j]) != std::string::npos) ++j;
        if (j < fmt.size() && std::string("diuoxX").find(fmt[j]) != std::string::npos) {
            integerFormatter = true;
            break;
        }
    }

    if (integerFormatter) {
        std::snprintf(buf, sizeof(buf), fmt.c_str(), (int)x);
    }
    else {
        std::snprintf(buf, sizeof(buf), fmt.c_str(), x);
    }
    return std::string(buf);
}

static const Value* findRowValue(const std::unordered_map<std::string, Value>& row,
                                 const std::string& id,
                                 const std::string& prefixHint = std::string()) {
    auto it = row.find(id);
    if (it != row.end()) return &it->second;

    if (!prefixHint.empty()) {
        const std::string hinted = prefixHint + " " + id;
        it = row.find(hinted);
        if (it != row.end()) return &it->second;
    }

    const std::string suffix = " " + id;
    const Value* found = nullptr;
    for (const auto& kv : row) {
        if (kv.first.size() < suffix.size()) continue;
        if (kv.first.compare(kv.first.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        if (found) return nullptr;
        found = &kv.second;
    }
    return found;
}

static std::string commonPrefixHint(const std::vector<ConcatPart>& parts) {
    std::string hint;
    for (const auto& p : parts) {
        if (p.kind != ConcatPartKind::Column) continue;
        const size_t sp = p.id.find(' ');
        if (sp == std::string::npos || sp == 0) continue;
        const std::string prefix = p.id.substr(0, sp);
        if (Utils::ieq(prefix, "LOOP")) continue;
        if (hint.empty()) hint = prefix;
        else if (hint != prefix) {
            // Keep the first concrete namespace as a hint. Some official
            // definitions mix one namespaced column with generic aliases.
            return hint;
        }
    }
    return hint;
}

static std::vector<std::string> splitUtf8Chars(const std::string& s) {
    std::vector<std::string> chars;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        size_t len = 1;
        if ((c & 0x80) == 0) {
            len = 1;
        }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            len = 2;
        }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            len = 3;
        }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            len = 4;
        }
        if (i + len > s.size()) len = 1;
        chars.push_back(s.substr(i, len));
        i += len;
    }
    return chars;
}

static Value applyOneValue(const std::unordered_map<std::string, FormatDef>& formats,
                           const FormatDef& f,
                           const std::unordered_map<std::string, Value>& row,
    const Value& in,
    int loopIndex) {

    auto toInt64 = [&](const Value& v) -> int64_t {
        if (auto* n = std::get_if<int64_t>(&v)) return *n;
        return std::atoll(valueToString(v).c_str());
        };
    auto toDouble = [&](const Value& v) -> double {
        if (auto* n = std::get_if<int64_t>(&v)) return (double)*n;
        return std::atof(valueToString(v).c_str());
        };


    auto evalParts = [&](const std::vector<ConcatPart>& parts) -> std::string {
        std::string out;
        const std::string prefixHint = commonPrefixHint(parts);
        for (const auto& p : parts) {
            if (p.kind == ConcatPartKind::Text) { out += p.text; continue; }

            const Value* source = nullptr;
            if (p.kind == ConcatPartKind::Input) {
                source = &in;
            }
            else {
                source = findRowValue(row, p.id, prefixHint);
                if (!source) continue;
            }

            if (!p.format.empty()) {
                Value formatted = Formatter::apply(formats, p.format, row, *source, loopIndex);
                out += valueToString(formatted);
            }
            else {
                out += valueToString(*source);
            }
        }
        return out;
    };
    Value effectiveInput = in;

    // sum/concat/min/max override input, then the rest of the format still applies.
    if (!f.sumCols.empty()) {
        int64_t sum = 0;
        for (auto& c : f.sumCols) {
            const Value* pv = findRowValue(row, c.id);
            if (!pv) continue;
            Value v = *pv;
            if (!c.format.empty()) v = Formatter::apply(formats, c.format, row, v, loopIndex);
            sum += toInt64(v);
        }
        effectiveInput = sum;
    }
    else if (!f.concatParts.empty()) {
        std::string out;
        if (!f.inputAsSubcolumnsInput && !std::holds_alternative<std::monostate>(in)) {
            out = valueToString(in);
        }
        const std::string prefixHint = commonPrefixHint(f.concatParts);

        for (const auto& p : f.concatParts) {
            if (p.kind == ConcatPartKind::Text) {
                out += p.text;
                continue;
            }

            Value v;

            if (p.kind == ConcatPartKind::Input) {
                v = f.inputAsSubcolumnsInput ? in : Value{};
            }
            else { // Column
                const Value* pv = findRowValue(row, p.id, prefixHint);
                if (!pv) {
                    if (Utils::ieq(f.id, "course_time") && Utils::ieq(p.id, "COURSE TIME_MS")) {
                        v = (int64_t)100;
                    }
                    else {
                        continue;
                    }
                }
                else {
                    v = *pv;
                }
            }

            if (!p.format.empty())
                v = Formatter::apply(formats, p.format, row, v, loopIndex);

            out += valueToString(v);
        }

        effectiveInput = out;
    }
    else if (!f.minCols.empty()) {
        bool have = false;
        int64_t best = 0;
        for (auto& c : f.minCols) {
            const Value* pv = findRowValue(row, c.id);
            if (!pv) continue;
            Value v = *pv;
            if (!c.format.empty()) v = Formatter::apply(formats, c.format, row, v, loopIndex);
            int64_t x = toInt64(v);
            if (!have || x < best) { best = x; have = true; }
        }
        effectiveInput = have ? Value(best) : Value(in);
    }
    else if (!f.maxCols.empty()) {
        bool have = false;
        int64_t best = 0;
        for (auto& c : f.maxCols) {
            const Value* pv = findRowValue(row, c.id);
            if (!pv) continue;
            Value v = *pv;
            if (!c.format.empty()) v = Formatter::apply(formats, c.format, row, v, loopIndex);
            int64_t x = toInt64(v);
            if (!have || x > best) { best = x; have = true; }
        }
        effectiveInput = have ? Value(best) : Value(in);
    }

    // Decide whether we must compute a numeric value first.
    const bool hasNumericOps =
        !f.mathOps.empty() ||
        !f.formatter.empty() ||
        f.doLoopIndex ||
        f.doRound ||
        f.doTrunc;

    // Start from input only if we are actually doing numeric work.
    double num = hasNumericOps ? toDouble(effectiveInput) : 0.0;

    if (hasNumericOps) {
        for (auto& op : f.mathOps) {
            const double a = op.second;
            switch (op.first) {
                case FormatKind::Add:        num += a; break;
                case FormatKind::Substract:  num -= a; break;
                case FormatKind::Multiply:   num *= a; break;
                case FormatKind::Divide:
                if (a != 0.0) { num /= a; }
                break;
                case FormatKind::Remainder: {
                    int64_t x = (int64_t)toInt64(Value((int64_t)std::llround(num)));
                    int64_t d = (int64_t)std::llround(a);
                    if (d != 0) num = (double)(x % d);
                } break;
                case FormatKind::DivideTrunc: {
                    int64_t x = (int64_t)toInt64(Value((int64_t)std::llround(num)));
                    int64_t d = (int64_t)std::llround(a);
                    if (d != 0) num = (double)(x / d);
                } break;
                case FormatKind::DivideRound: {
                    int64_t x = (int64_t)toInt64(Value((int64_t)std::llround(num)));
                    int64_t d = (int64_t)std::llround(a);
                    if (d != 0) num = (double)std::llround((double)x / (double)d);
                } break;
                case FormatKind::Shift: {
                    int sh = (int)std::llround(a);
                    if (sh < 0) sh = 0;
                    uint64_t x = (uint64_t)toInt64(Value((int64_t)std::llround(num)));
                    x <<= (unsigned)sh;
                    num = (double)(int64_t)x;
                } break;
            }
        }

        if (f.doLoopIndex && loopIndex >= 0) { num = (double)loopIndex; }
        if (f.doRound) { num = (double)std::llround(num); }
        if (f.doTrunc) { num = (double)(int64_t)num; }
    }

    // Convert to string.
    // If numeric work happened, stringify the computed number; otherwise stringify the input.
    std::string s = hasNumericOps
        ? formatNumberWithFormatter(num, f.formatter)
        : valueToString(effectiveInput);


    // cases
    if (!f.cases.empty()) {

        auto isHexCaseToken = [](const std::string& raw) -> bool {
            std::string t = Utils::trim(raw);
            if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) return true;
            for (char ch : t) {
                if ((ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) return true;
            }
            return false;
            };

        // If ANY case src looks hex-like, treat ALL numeric case src tokens as hex (base 16),
        // matching original hi2txt behavior for tables like 90..B3.
        bool hexCaseTable = false;
        for (const auto& c : f.cases) {
            if (!c.isDefault && isHexCaseToken(c.src)) { hexCaseTable = true; break; }
        }

        auto parseSrcValue = [&](const std::string& raw, Value& out) -> bool {
            std::string t = Utils::trim(raw);
            if (raw.empty()) { out = std::string(); return true; }

            if (Utils::ieq(f.id, "bcd_byte") &&
                t.size() == 4 &&
                t[0] == '0' && (t[1] == 'x' || t[1] == 'X') &&
                std::isdigit((unsigned char)t[2]) &&
                std::isdigit((unsigned char)t[3])) {
                out = (int64_t)((t[2] - '0') * 10 + (t[3] - '0'));
                return true;
            }

            // Hex-table mode: accept bare hex (no 0x) for digit-only tokens too.
            if (hexCaseTable) {
                size_t i = 0;
                if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) i = 2;

                bool allHex = (i < t.size());
                for (size_t j = i; j < t.size(); ++j) {
                    char ch = t[j];
                    const bool ok =
                        (ch >= '0' && ch <= '9') ||
                        (ch >= 'A' && ch <= 'F') ||
                        (ch >= 'a' && ch <= 'f');
                    if (!ok) { allHex = false; break; }
                }

                if (allHex) {
                    const char* p = t.c_str() + i;
                    char* endp = nullptr;
                    long long v = std::strtoll(p, &endp, 16);
                    if (endp && *endp == '\0') { out = (int64_t)v; return true; }
                }
            }

            // Default behavior: decimal unless 0x/0X
            bool ok = false;
            int64_t n = Utils::parseInt64Auto(t, &ok);
            if (ok) { out = n; return true; }

            out = raw; // string compare fallback; preserve intentional padding
            return true;
            };

        auto cmpValues = [&](const Value& a, const Value& b) -> int {

            auto rawToInt = [](const Value& v, int64_t& out) -> bool {
                if (auto* n = std::get_if<int64_t>(&v)) {
                    out = *n;
                    return true;
                }
                if (auto* s = std::get_if<std::string>(&v)) {
                    bool ok = false;
                    out = Utils::parseInt64Auto(*s, &ok);
                    if (ok) return true;

                    // hi2txt character maps often compare raw one-byte text
                    // values against numeric/hex case sources, e.g. 0x0F -> P.
                    if (s->size() == 1) {
                        out = (unsigned char)(*s)[0];
                        return true;
                    }
                    return false;
                }
                if (auto* rb = std::get_if<RawBytes>(&v)) {
                    if (rb->empty() || rb->size() > 8) return false;
                    uint64_t acc = 0;
                    for (uint8_t x : *rb) acc = (acc << 8) | (uint64_t)x;
                    out = (int64_t)acc;
                    return true;
                }
                return false;
                };

            int64_t ai = 0, bi = 0;
            bool aNum = rawToInt(a, ai);
            bool bNum = rawToInt(b, bi);

            // numeric compare if both numeric-compatible
            if (aNum && bNum) {
                if (ai < bi) return -1;
                if (ai > bi) return 1;
                return 0;
            }

            // fallback string compare (original behavior)
            const std::string x = Utils::valueToString(a);
            const std::string y = Utils::valueToString(b);

            if (x < y) return -1;
            if (x > y) return 1;
            return 0;
            };

        auto matchOp = [&](int c, const std::string& opRaw) -> bool {
            const std::string op = Utils::trim(opRaw);
            if (op.empty() || op == "=" || Utils::ieq(op, "==")) return c == 0;
            if (op == "!=") return c != 0;
            if (op == "<")  return c < 0;
            if (op == "<=") return c <= 0;
            if (op == ">")  return c > 0;
            if (op == ">=") return c >= 0;
            return false;
            };

        bool matched = false;
        CaseMap defCase;
        bool haveDefault = false;

        for (const auto& c : f.cases) {
            if (c.isDefault) { defCase = c; haveDefault = true; continue; }

            Value lhs = hasNumericOps ? Value((int64_t)std::llround(num)) : effectiveInput;
            if (!c.operatorFormat.empty())
                lhs = Formatter::apply(formats, c.operatorFormat, row, lhs, loopIndex);

            Value rhs;
            parseSrcValue(c.src, rhs);

            const int cmp = cmpValues(lhs, rhs);
            const std::string op = c.op.empty() ? "==" : c.op;

            if (!matchOp(cmp, op)) continue;

            if (c.hasDst) {
                s = c.dst; // may be ""
            }
            else if (!c.format.empty()) {
                Value v = Formatter::apply(formats, c.format, row, lhs, loopIndex);
                s = valueToString(v);
            }
            matched = true;
            break;
        }

        if (!matched && haveDefault) {
            if (defCase.hasDst) {
                s = defCase.dst;
            }
            else if (!defCase.format.empty()) {
                Value lhs = hasNumericOps ? Value((int64_t)std::llround(num)) : effectiveInput;
                Value v = Formatter::apply(formats, defCase.format, row, lhs, loopIndex);
                s = valueToString(v);
            }
        }
    }


    // casing
    if (f.lowercase) s = toLower(s);
    if (f.uppercase) s = toUpper(s);
    if (f.capitalize) s = toCap(s);

    // replace, trim, pad
    if (!f.repls.empty()) {
        for (const auto& r : f.repls) s = doReplace(s, r);
    }
    else {
        s = doReplace(s, f.repl);
    }
    if (!f.trims.empty()) {
        for (const auto& t : f.trims) s = doTrim(s, t);
    }
    else {
        s = doTrim(s, f.trim);
    }
    s = doPad(s, f.pad);

    // prefix/suffix
    auto isEmptyInput = [&](const AffixOp& a, const std::string& cur) -> bool {
        if (a.hasEmpty) return cur == a.emptyValue;
        return cur.empty();
    };

    for (const auto& pre : f.prefixes) {
        if (isEmptyInput(pre, s)) {
            if (pre.consume && pre.hasEmpty) return std::string(); // consume => no output
            continue; // nothing appended
        }
        const std::string add = evalParts(pre.parts);
        if (!add.empty()) s = add + s;
    }

    for (const auto& suf : f.suffixes) {
        if (isEmptyInput(suf, s)) {
            if (suf.consume && suf.hasEmpty) return std::string(); // consume => no output
            continue;
        }
        const std::string add = evalParts(suf.parts);
        if (!add.empty()) s = s + add;
    }

    return s;
}

static Value applyOne(const std::unordered_map<std::string, FormatDef>& formats,
    const FormatDef& f,
    const std::unordered_map<std::string, Value>& row,
    const Value& in,
    int loopIndex) {

    if (f.applyTo == ApplyToKind::Char) {
        std::string src = valueToString(in);
        std::string out;
        out.reserve(src.size() * 2);

        for (const auto& ch : splitUtf8Chars(src)) {
            Value v = ch;

            // apply operations once-per-char (force value mode inside)
            FormatDef tmp = f;
            tmp.applyTo = ApplyToKind::Value;
            out += valueToString(applyOneValue(formats, tmp, row, v, loopIndex));
        }
        return out;
    }

    return applyOneValue(formats, f, row, in, loopIndex);
}


Value Formatter::apply(const std::unordered_map<std::string, FormatDef>& formats,
    const std::string& fmtExpr,
    const std::unordered_map<std::string, Value>& row,
    const Value& in, int loopIndex) {
    std::string chain = fmtExpr;
    if (trim(chain).empty()) return in;

    {
        const std::string direct = trim(chain);
        if (direct.size() > 1 && (direct[0] == 'x' || direct[0] == 'X')) {
            bool digits = true;
            for (size_t j = 1; j < direct.size(); ++j) {
                if (!std::isdigit((unsigned char)direct[j])) { digits = false; break; }
            }
            if (digits) {
                const int64_t factor = std::strtoll(direct.c_str() + 1, nullptr, 10);
                if (auto* n = std::get_if<int64_t>(&in)) return (int64_t)(*n * factor);
                return (int64_t)(std::atoll(valueToString(in).c_str()) * factor);
            }
        }
    }

    auto applyInline = [&](const std::string& op, Value& cur) -> bool {
        if (op.empty()) return false;

        auto toInt = [&](const Value& v) -> int64_t {
            if (auto* n = std::get_if<int64_t>(&v)) return *n;
            return std::atoll(valueToString(v).c_str());
            };
        auto toDouble = [&](const Value& v) -> double {
            if (auto* n = std::get_if<int64_t>(&v)) return (double)*n;
            return std::atof(valueToString(v).c_str());
            };

        auto isAllInt = [&](const char* s) -> bool {
            if (!s || !*s) return false;
            const char* p = s;
            if (*p == '+' || *p == '-') ++p;
            if (!*p) return false;
            while (*p) { if (*p < '0' || *p > '9') return false; ++p; }
            return true;
            };

        // LoopIndex token
        if (ieq(op, "loopindex") || ieq(op, "LoopIndex")) {
            if (loopIndex >= 0) cur = (int64_t)loopIndex;
            return true;
        }

        // Case tokens
        if (ieq(op, "LC") || ieq(op, "Lowercase")) { cur = toLower(valueToString(cur)); return true; }
        if (ieq(op, "UC") || ieq(op, "Uppercase")) { cur = toUpper(valueToString(cur)); return true; }
        if (ieq(op, "Capitalize")) { cur = toCap(valueToString(cur)); return true; }

        // Round/Trunc tokens
        if (ieq(op, "R") || ieq(op, "Round")) {
            cur = (int64_t)std::llround(toDouble(cur));
            return true;
        }
        if (ieq(op, "T") || ieq(op, "Trunc")) {
            cur = (int64_t)(toDouble(cur));
            return true;
        }

        // Trim tokens: TrimLx / TrimRx / Trimx. Bare "TrimR" is Trim + char 'R'.
        if (op.rfind("TrimL", 0) == 0 && op.size() > 5) {
            std::string chars = op.substr(5);
            TrimOp t; t.dir = TrimDir::Left; t.chars = chars.empty() ? " " : chars;
            cur = doTrim(valueToString(cur), t);
            return true;
        }
        if (op.rfind("TrimR", 0) == 0 && op.size() > 5) {
            std::string chars = op.substr(5);
            TrimOp t; t.dir = TrimDir::Right; t.chars = chars.empty() ? " " : chars;
            cur = doTrim(valueToString(cur), t);
            return true;
        }
        if (op.rfind("Trim", 0) == 0) {
            std::string chars = op.substr(4);
            TrimOp t; t.dir = TrimDir::Both; t.chars = chars.empty() ? " " : chars;
            cur = doTrim(valueToString(cur), t);
            return true;
        }

        // Pad tokens: PadL<digits><char> / PadR<digits><char>
// Pad tokens: PadL<width><char> / PadR<width><char>
// GreatStone shorthand: if suffix is ALL digits and length>=2, last digit is pad char.
// Example: PadL50 => width=5, pad='0'
        auto parsePad = [&](bool right) -> bool {
            const std::string prefix = right ? "PadR" : "PadL";
            if (op.rfind(prefix, 0) != 0) return false;

            std::string rest = op.substr(prefix.size());

            // split into leading digits + remainder
            size_t i = 0;
            while (i < rest.size() && std::isdigit((unsigned char)rest[i])) ++i;

            if (i == 0) return true; // treated as no-op

            int max = 0;
            std::string ch;

            // If rest is all digits, support shorthand where last digit is pad char.
            if (i == rest.size() && i >= 2) {
                max = std::atoi(rest.substr(0, i - 1).c_str());
                ch = rest.substr(i - 1, 1);
            }
            else {
                max = std::atoi(rest.substr(0, i).c_str());
                ch = rest.substr(i); // may be empty or non-digit pad string
            }

            PadOp p;
            p.enabled = true;
            p.dir = right ? TrimDir::Right : TrimDir::Left;
            p.max = max;
            p.padChar = ch.empty() ? "0" : ch;

            cur = doPad(valueToString(cur), p);
            return true;
            };
        if (parsePad(false)) return true;
        if (parsePad(true)) return true;

        // Prefix/Suffix tokens
        if (op.rfind("Prefix", 0) == 0) { cur = op.substr(6) + valueToString(cur); return true; }
        if (op.rfind("Suffix", 0) == 0) { cur = valueToString(cur) + op.substr(6); return true; }

        // 0x => hex string
        if (ieq(op, "0x")) {
            int64_t n = toInt(cur);
            char buf[64];
            if (n < 0) std::snprintf(buf, sizeof(buf), "-0x%llx", (unsigned long long)(-n));
            else       std::snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)n);
            cur = std::string(buf);
            return true;
        }

        // shift implicit: >N
        if (op.size() > 1 && op[0] == '>' && isAllInt(op.c_str() + 1)) {
            int sh = std::atoi(op.c_str() + 1);
            if (sh < 0) sh = 0;
            uint64_t x = (uint64_t)toInt(cur);
            x <<= (unsigned)sh;
            cur = (int64_t)x;
            return true;
        }

        // remainder: %N
        if (op.size() > 1 && op[0] == '%' && isAllInt(op.c_str() + 1)) {
            long long d = std::strtoll(op.c_str() + 1, nullptr, 10);
            if (d != 0) cur = (int64_t)(toInt(cur) % (int64_t)d);
            return true;
        }

        // divide_trunc: dN (lowercase d)
        if (op.size() > 1 && op[0] == 'd' && isAllInt(op.c_str() + 1)) {
            long long d = std::strtoll(op.c_str() + 1, nullptr, 10);
            if (d != 0) cur = (int64_t)(toInt(cur) / (int64_t)d);
            return true;
        }

        // divide_round: DN (uppercase D)
        if (op.size() > 1 && op[0] == 'D' && isAllInt(op.c_str() + 1)) {
            long long d = std::strtoll(op.c_str() + 1, nullptr, 10);
            if (d != 0) cur = (int64_t)std::llround((double)toInt(cur) / (double)d);
            return true;
        }

        // +n / -n
        if ((op[0] == '+' || op[0] == '-') && op.size() > 1) {
            if (isAllInt(op.c_str())) {
                long long n = std::strtoll(op.c_str(), nullptr, 10);
                cur = (int64_t)(toInt(cur) + (int64_t)n);
                return true;
            }
        }

        // *n, xN/XN => multiply
        if ((op[0] == '*' || op[0] == 'x' || op[0] == 'X') && op.size() > 1) {
            const char* s = op.c_str() + 1;
            if (isAllInt(s)) {
                long long n = std::strtoll(s, nullptr, 10);
                cur = (int64_t)(toInt(cur) * (int64_t)n);
                return true;
            }
        }

        // /n => float division (string)
        if (op[0] == '/' && op.size() > 1) {
            const char* s = op.c_str() + 1;
            if (isAllInt(s)) {
                long long d = std::strtoll(s, nullptr, 10);
                if (d != 0) {
                    double v = (double)toInt(cur) / (double)d;
                    cur = formatNumberWithFormatter(v, ""); // default float stringify
                }
                return true;
            }
        }

        return false;
        };

    // tokenize by ';' or '|'
    Value cur = in;
    std::string tok;
    for (size_t i = 0; i <= chain.size(); ++i) {
        const char ch = (i < chain.size()) ? chain[i] : '\0';
        if (ch == ';' || ch == '|' || ch == '\0') {
            std::string evalTok = trim(tok);
            std::string lookupTok = evalTok;
            const std::string leftTrimmed = tok.substr(tok.find_first_not_of(" \t\r\n") == std::string::npos ? tok.size() : tok.find_first_not_of(" \t\r\n"));
            if (leftTrimmed.rfind("Trim", 0) == 0) evalTok = leftTrimmed;

            if (!evalTok.empty()) {
                auto it = formats.find(lookupTok);
                if (it != formats.end()) {
                    cur = applyOne(formats, it->second, row, cur, loopIndex);
                }
                else {
                    applyInline(evalTok, cur);
                }
            }
            tok.clear();
        }
        else {
            tok.push_back(ch);
        }
    }

    return cur;
}


} // namespace openhi2txt
