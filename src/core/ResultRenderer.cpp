#include "core/ResultRenderer.h"
#include "core/Formatter.h"
#include "core/Trace.h"
#include "io/Utils.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace openhi2txt {

namespace {

static inline bool valueEmpty(const Value& v) {
    return std::holds_alternative<std::monostate>(v);
}

static const char* kUnsortedIndexKey = "__hi2txt_unsorted_index";

static bool displayAllowed(const std::string& disp, const ReadOptions& options) {
    if (disp.empty()) return true;
    if (Utils::ieq(disp, "extra")) return options.includeExtra;
    if (Utils::ieq(disp, "debug")) return options.includeDebug;
    return false;
}

static DisplayLevel displayLevelOf(const std::string& disp) {
    if (Utils::ieq(disp, "debug")) return DisplayLevel::Debug;
    if (Utils::ieq(disp, "extra")) return DisplayLevel::Extra;
    return DisplayLevel::Always;
}

static bool containsCi(const std::vector<std::string>& values, const std::string& wanted) {
    for (const auto& v : values) {
        if (Utils::ieq(v, wanted)) return true;
    }
    return false;
}

static bool optionAllowsColumn(const Column& col, const ReadOptions& options) {
    if (!displayAllowed(col.display, options)) return false;
    if (!options.keepFields.empty() && !containsCi(options.keepFields, col.id)) return false;
    if (containsCi(options.hideFields, col.id)) return false;
    return true;
}

static std::string groupScoreValue(const std::string& value, const ReadOptions& options) {
    if (!options.scoreGrouping || options.scoreGroupingSize <= 0 || value.empty()) return value;

    size_t firstDigit = 0;
    bool negative = false;
    if (value[0] == '+' || value[0] == '-') {
        negative = value[0] == '-';
        firstDigit = 1;
    }

    if (firstDigit >= value.size()) return value;
    for (size_t i = firstDigit; i < value.size(); ++i) {
        if (value[i] < '0' || value[i] > '9') return value;
    }

    std::string digits = value.substr(firstDigit);
    std::string grouped;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count > 0 && count % options.scoreGroupingSize == 0) {
            grouped.append(options.scoreGroupingSeparator.rbegin(), options.scoreGroupingSeparator.rend());
        }
        grouped.push_back(*it);
        ++count;
    }
    if (negative) grouped.push_back('-');
    std::reverse(grouped.begin(), grouped.end());
    return grouped;
}

static bool rowMatchesKeepTableValues(const std::unordered_map<std::string, std::string>& rowValues,
                                      const ReadOptions& options) {
    if (options.keepTableValues.empty()) return true;

    for (const auto& rule : options.keepTableValues) {
        for (const auto& kv : rowValues) {
            if (Utils::ieq(kv.first, rule.first) && Utils::ieq(kv.second, rule.second)) {
                return true;
            }
        }
    }
    return false;
}

static std::vector<std::string> splitFormatChain(const std::string& chain) {
    std::vector<std::string> out;
    std::stringstream ss(chain);
    std::string tok;
    while (std::getline(ss, tok, ';')) {
        tok = Utils::trim(tok);
        if (!tok.empty()) out.push_back(tok);
    }
    return out;
}

static bool isInlineFormatToken(const std::string& tok) {
    if (tok.empty()) return true;
    const char c0 = tok[0];
    if (c0 == '+' || c0 == '-' || c0 == '*' || c0 == '/' || c0 == 'x' || c0 == 'X' || c0 == '>') return true;
    if (tok.rfind("<<", 0) == 0 || tok.rfind(">>", 0) == 0) return true;
    if (tok.rfind("0x", 0) == 0 || tok.rfind("0X", 0) == 0) return true;
    if (Utils::ieq(tok, "TrimL0") || Utils::ieq(tok, "TrimR") || Utils::ieq(tok, "Trim ") || Utils::ieq(tok, "trim")) return true;
    if (Utils::ieq(tok, "hexadecimal_string") || Utils::ieq(tok, "hex")) return true;
    return false;
}

static void collectFormatDepsDfs(
    const std::unordered_map<std::string, FormatDef>& fmts,
    const std::string& fmtId,
    std::unordered_set<std::string>& out) {
    auto it = Utils::findIdentifier(fmts, fmtId);
    if (it == fmts.end()) return;
    const FormatDef& f = it->second;

    for (const auto& r : f.sumCols) if (!r.id.empty()) out.insert(r.id);
    for (const auto& p : f.concatParts)
        if (p.kind == ConcatPartKind::Column && !p.id.empty()) out.insert(p.id);
    for (const auto& r : f.minCols) if (!r.id.empty()) out.insert(r.id);
    for (const auto& r : f.maxCols) if (!r.id.empty()) out.insert(r.id);

    auto visitRefList = [&](const auto& refs) {
        for (const auto& r : refs) {
            if (r.format.empty()) continue;
            for (const auto& tok : splitFormatChain(r.format)) {
                if (Utils::findIdentifier(fmts, tok) != fmts.end()) {
                    collectFormatDepsDfs(fmts, tok, out);
                    continue;
                }
                if (!isInlineFormatToken(tok)) collectFormatDepsDfs(fmts, tok, out);
            }
        }
    };

    visitRefList(f.sumCols);
    visitRefList(f.minCols);
    visitRefList(f.maxCols);

    for (const auto& p : f.concatParts) {
        if (p.format.empty()) continue;
        for (const auto& tok : splitFormatChain(p.format)) {
            if (Utils::findIdentifier(fmts, tok) != fmts.end()) {
                collectFormatDepsDfs(fmts, tok, out);
                continue;
            }
            if (!isInlineFormatToken(tok)) collectFormatDepsDfs(fmts, tok, out);
        }
    }
}

static std::unordered_set<std::string> collectFormatDeps(
    const std::unordered_map<std::string, FormatDef>& fmts,
    const std::string& fmtChain) {
    std::unordered_set<std::string> deps;
    for (const auto& tok : splitFormatChain(fmtChain)) {
        if (Utils::findIdentifier(fmts, tok) != fmts.end()) {
            collectFormatDepsDfs(fmts, tok, deps);
            continue;
        }
        if (!isInlineFormatToken(tok)) collectFormatDepsDfs(fmts, tok, deps);
    }
    return deps;
}

static bool rowRelevantToTable(const GameDef& def,
    const Table& tab,
    const std::unordered_map<std::string, Value>& row,
    const ReadOptions& options) {
    auto colHasData = [&](const Column& col) -> bool {
        const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
        Value value = std::monostate{};
        bool sourcePresent = false;

        if (!src.empty() && !Utils::ieq(src, "index") && !Utils::ieq(src, "unsorted_index")) {
            auto it = Utils::findIdentifier(row, src);
            if (it != row.end()) {
                value = it->second;
                sourcePresent = true;
            }
        }

        if (!col.format.empty()) {
            if (!sourcePresent) {
                bool synthesizesEmpty = false;
                for (const auto& token : splitFormatChain(col.format)) {
                    auto format = Utils::findIdentifier(def.formats, token);
                    if (format == def.formats.end()) continue;
                    for (const auto& caseMap : format->second.cases) {
                        if (caseMap.isDefault || caseMap.src.empty()) {
                            synthesizesEmpty = true;
                            break;
                        }
                    }
                    if (synthesizesEmpty) break;
                }
                if (!synthesizesEmpty) {
                    const auto dependencies = collectFormatDeps(def.formats, col.format);
                    for (const auto& dependency : dependencies) {
                        if (Utils::findIdentifier(row, dependency) != row.end()) {
                            synthesizesEmpty = true;
                            break;
                        }
                    }
                }
                if (!synthesizesEmpty) return false;
            }
            auto mutableRow = row;
            value = Formatter::apply(def.formats, col.format, mutableRow, value);
        }

        return sourcePresent || !Utils::valueToString(value).empty();
    };

    // The normal-output columns define row population even when extra/debug
    // columns are enabled. Only fall back to conditional columns when there
    // is no ordinary non-index column.
    const Column* rangeColumn = nullptr;
    for (const auto& col : tab.cols) {
        if (col.sourceRow == SourceRowKind::OutputIndex) continue;
        const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
        if (Utils::ieq(src, "index") || Utils::ieq(src, "unsorted_index")) continue;
        if (col.display.empty()) {
            rangeColumn = &col;
            break;
        }
    }
    if (!rangeColumn) {
        for (const auto& col : tab.cols) {
            if (col.sourceRow == SourceRowKind::OutputIndex) continue;
            const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
            if (Utils::ieq(src, "index") || Utils::ieq(src, "unsorted_index")) continue;
            if (displayAllowed(col.display, options)) {
                rangeColumn = &col;
                break;
            }
        }
    }
    if (!rangeColumn) {
        for (const auto& col : tab.cols) {
            if (col.sourceRow == SourceRowKind::OutputIndex) continue;
            const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
            if (!Utils::ieq(src, "index") && !Utils::ieq(src, "unsorted_index")) {
                rangeColumn = &col;
                break;
            }
        }
    }
    if (!rangeColumn) return true;
    if (colHasData(*rangeColumn)) return true;

    // A line-ignore rule can make a later field the authoritative row
    // sentinel (Tempest stores its final score without a name).
    if (!tab.ignoreRules.empty()) {
        for (const auto& col : tab.cols) {
            if (col.sourceRow == SourceRowKind::OutputIndex) continue;
            const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
            if (Utils::ieq(src, "index") || Utils::ieq(src, "unsorted_index")) continue;
            if (!displayAllowed(col.display, options)) continue;

            bool hasInput = Utils::findIdentifier(row, src) != row.end();
            if (!hasInput && !col.format.empty()) {
                const auto dependencies = collectFormatDeps(def.formats, col.format);
                for (const auto& dependency : dependencies) {
                    if (Utils::findIdentifier(row, dependency) != row.end()) {
                        hasInput = true;
                        break;
                    }
                }
            }

            // A default format may turn a missing value into text such as
            // "0". That must not populate rows belonging to another table.
            if (hasInput && colHasData(col)) return true;
        }
    }

    return false;
}

static bool rowShouldIgnore(const Table& tab,
    const std::unordered_map<std::string, Value>& row,
    const std::unordered_map<std::string, FormatDef>& formats,
    size_t rowIdx) {
    if (tab.ignoreRules.empty()) return false;

    auto matchRule = [&](const IgnoreRule& r) -> bool {
        Value v = std::monostate{};
        const Column* outputColumn = nullptr;
        for (const auto& col : tab.cols) {
            if (Utils::ieq(col.id, r.colId)) {
                outputColumn = &col;
                break;
            }
        }

        if (outputColumn) {
            const std::string src = Utils::trim(outputColumn->src).empty()
                ? outputColumn->id
                : outputColumn->src;
            if (Utils::ieq(src, "index")) {
                v = (int64_t)rowIdx;
            }
            else if (Utils::ieq(src, "unsorted_index")) {
                auto ui = row.find(kUnsortedIndexKey);
                v = (ui != row.end()) ? ui->second : Value((int64_t)rowIdx);
            }
            else {
                auto srcIt = Utils::findIdentifier(row, src);
                if (srcIt != row.end()) v = srcIt->second;
            }

            if (!outputColumn->format.empty()) {
                v = Formatter::apply(formats, outputColumn->format,
                    const_cast<std::unordered_map<std::string, Value>&>(row),
                    v,
                    (int)rowIdx);
            }
        }
        else {
            auto it = Utils::findIdentifier(row, r.colId);
            if (it != row.end()) v = it->second;
        }

        if (!r.value.empty() && r.value[0] == '#') {
            const std::string fmtChain = r.value.substr(1);
            v = Formatter::apply(formats, fmtChain, const_cast<std::unordered_map<std::string, Value>&>(row), v);
        }
        const std::string got = Utils::valueToString(v);
        if (!tab.ignoreCompareOp.empty()) {
            bool aOk = false;
            bool bOk = false;
            const int64_t a = Utils::parseInt64Auto(got, &aOk);
            const int64_t b = Utils::parseInt64Auto(r.value, &bOk);
            if (aOk && bOk) {
                if (tab.ignoreCompareOp == ">") return a > b;
                if (tab.ignoreCompareOp == ">=") return a >= b;
                if (tab.ignoreCompareOp == "<") return a < b;
                if (tab.ignoreCompareOp == "<=") return a <= b;
                if (tab.ignoreCompareOp == "!=") return a != b;
                if (tab.ignoreCompareOp == "=" || Utils::ieq(tab.ignoreCompareOp, "==")) return a == b;
            }
            if (tab.ignoreCompareOp == "!=") return !Utils::ieq(got, r.value);
            if (tab.ignoreCompareOp == "=" || Utils::ieq(tab.ignoreCompareOp, "==")) return Utils::ieq(got, r.value);
        }
        return Utils::ieq(got, r.value);
    };

    if (tab.ignoreOp == IgnoreOp::Or) {
        for (const auto& r : tab.ignoreRules) if (matchRule(r)) return true;
        return false;
    }

    for (const auto& r : tab.ignoreRules) if (!matchRule(r)) return false;
    return true;
}

static int compareValueForSort(const Value& va, const Value& vb) {
    if (std::holds_alternative<int64_t>(va) && std::holds_alternative<int64_t>(vb)) {
        const int64_t a = std::get<int64_t>(va);
        const int64_t b = std::get<int64_t>(vb);
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }

    auto numericCompatible = [](const Value& v, int64_t& out) -> bool {
        if (auto* n = std::get_if<int64_t>(&v)) {
            out = *n;
            return true;
        }
        if (auto* s = std::get_if<std::string>(&v)) {
            bool ok = false;
            out = Utils::parseInt64Auto(*s, &ok);
            return ok;
        }
        return false;
    };

    int64_t ai = 0;
    int64_t bi = 0;
    if (numericCompatible(va, ai) && numericCompatible(vb, bi)) {
        if (ai < bi) return -1;
        if (ai > bi) return 1;
        return 0;
    }

    const std::string a = Utils::valueToString(va);
    const std::string b = Utils::valueToString(vb);
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

static Value sortKeyValue(const std::unordered_map<std::string, Value>& row,
    const Table& tab,
    const SortKeyDef& key,
    const std::unordered_map<std::string, FormatDef>& formats) {
    if (Utils::ieq(key.src, "index")) return (int64_t)0;

    const Column* sortCol = nullptr;
    for (const auto& col : tab.cols) {
        if (Utils::ieq(col.id, key.src)) {
            sortCol = &col;
            break;
        }
    }

    Value v = std::monostate{};
    bool fromColumnAlias = false;

    if (sortCol) {
        const std::string src = Utils::trim(sortCol->src).empty() ? sortCol->id : sortCol->src;
        if (Utils::ieq(src, "index")) {
            v = (int64_t)0;
            fromColumnAlias = true;
        }
        else if (Utils::ieq(src, "unsorted_index")) {
            auto it = row.find(kUnsortedIndexKey);
            v = (it != row.end()) ? it->second : Value{};
            fromColumnAlias = true;
        }
        else {
            auto value = Utils::findIdentifier(row, src);
            if (value != row.end()) {
                v = value->second;
                fromColumnAlias = true;
            }
        }
    }

    if (!fromColumnAlias) {
        auto value = Utils::findIdentifier(row, key.src);
        if (value != row.end()) v = value->second;
    }

    if (!key.format.empty()) {
        v = Formatter::apply(formats, key.format,
            const_cast<std::unordered_map<std::string, Value>&>(row),
            v);
    }

    return v;
}

static Value sortKeyValue(const std::unordered_map<std::string, Value>& row,
    const Table& tab,
    const std::unordered_map<std::string, FormatDef>& formats) {
    const SortKeyDef key{ tab.sortKey, tab.sortOrder, tab.sortFormat };
    return sortKeyValue(row, tab, key, formats);
}

static int compareRowsThreeWay(const std::unordered_map<std::string, Value>& a,
    const std::unordered_map<std::string, Value>& b,
    const Table& tab,
    const std::unordered_map<std::string, FormatDef>& formats) {
    const std::vector<SortKeyDef> fallback{ { tab.sortKey, tab.sortOrder, tab.sortFormat } };
    const auto& keys = tab.sortKeys.empty() ? fallback : tab.sortKeys;
    for (const auto& key : keys) {
        const int cmp = compareValueForSort(
            sortKeyValue(a, tab, key, formats), sortKeyValue(b, tab, key, formats));
        if (cmp) return Utils::ieq(key.order, "desc") ? -cmp : cmp;
    }
    return 0;
}

static bool compareRows(const std::unordered_map<std::string, Value>& a,
    const std::unordered_map<std::string, Value>& b,
    const Table& tab,
    const std::unordered_map<std::string, FormatDef>& formats) {
    return compareRowsThreeWay(a, b, tab, formats) < 0;
}

template <typename Row, typename Compare>
static void djgppQsort(std::vector<Row>& values, Compare compare) {
    constexpr ptrdiff_t threshold = 4;
    constexpr ptrdiff_t medianThreshold = 6;
    auto swap = [&](ptrdiff_t a, ptrdiff_t b) { std::swap(values[(size_t)a], values[(size_t)b]); };
    std::function<void(ptrdiff_t, ptrdiff_t)> quicksort = [&](ptrdiff_t base, ptrdiff_t max) {
        ptrdiff_t length = max - base;
        do {
            ptrdiff_t middle = base + (length >> 1);
            if (length >= medianThreshold) {
                const ptrdiff_t first = base;
                ptrdiff_t pivot = compare(values[(size_t)first], values[(size_t)middle]) > 0 ? first : middle;
                const ptrdiff_t last = max - 1;
                if (compare(values[(size_t)pivot], values[(size_t)last]) > 0) {
                    pivot = pivot == first ? middle : first;
                    if (compare(values[(size_t)pivot], values[(size_t)last]) < 0) pivot = last;
                }
                if (pivot != middle) swap(pivot, middle);
            }

            ptrdiff_t left = base, right = max - 1;
            for (;;) {
                while (left < middle && compare(values[(size_t)left], values[(size_t)middle]) <= 0) ++left;
                while (right > middle) {
                    if (compare(values[(size_t)middle], values[(size_t)right]) <= 0) { --right; continue; }
                    ptrdiff_t next = left + 1, other;
                    if (left == middle) middle = other = right;
                    else { other = right; --right; }
                    swap(left, other); left = next;
                    goto partition_continue;
                }
                if (left == middle) break;
                else {
                    const ptrdiff_t other = middle;
                    middle = left;
                    --right;
                    swap(left, other);
                }
                partition_continue:;
            }

            const ptrdiff_t after = middle + 1;
            const ptrdiff_t lowLength = middle - base;
            const ptrdiff_t highLength = max - after;
            if (lowLength <= highLength) {
                if (lowLength >= threshold) quicksort(base, middle);
                base = after; length = highLength;
            }
            else {
                if (highLength >= threshold) quicksort(after, max);
                max = middle; length = lowLength;
            }
        } while (length >= threshold);
    };

    const ptrdiff_t count = (ptrdiff_t)values.size();
    if (count <= 1) return;
    ptrdiff_t high;
    if (count >= threshold) { quicksort(0, count); high = threshold; }
    else high = count;
    ptrdiff_t minimum = 0;
    for (ptrdiff_t i = 1; i < high; ++i)
        if (compare(values[(size_t)minimum], values[(size_t)i]) > 0) minimum = i;
    if (minimum) swap(0, minimum);
    for (ptrdiff_t i = 1; i < count; ++i) {
        ptrdiff_t destination = i;
        while (compare(values[(size_t)(destination - 1)], values[(size_t)i]) > 0) --destination;
        if (destination != i) {
            Row value = std::move(values[(size_t)i]);
            for (ptrdiff_t j = i; j > destination; --j)
                values[(size_t)j] = std::move(values[(size_t)(j - 1)]);
            values[(size_t)destination] = std::move(value);
        }
    }
}

static bool reverseEqualSortGroups(const Table& tab) {
    return tab.sortKey.find('_') == std::string::npos;
}

static std::vector<std::unordered_map<std::string, Value>> buildRankedPointsRows(
    const Table& tab,
    const std::vector<std::unordered_map<std::string, Value>>& sourceRows) {
    std::unordered_map<std::string, int64_t> totals;
    std::unordered_set<std::string> knownNames;
    std::vector<std::string> nameOrder;

    for (const auto& qualifier : tab.rankedPoints.sources) {
        std::unordered_set<std::string> creditedByQualifier;
        const size_t count = std::min(sourceRows.size(),
            static_cast<size_t>(qualifier.maxPoints));
        for (size_t rank = 0; rank < count; ++rank) {
            const auto value = Utils::findIdentifier(sourceRows[rank], qualifier.src);
            if (value == sourceRows[rank].end()) continue;

            const std::string name = Utils::trim(Utils::valueToString(value->second));
            if (name.empty() || !creditedByQualifier.insert(name).second) continue;

            if (knownNames.insert(name).second) nameOrder.push_back(name);
            totals[name] += qualifier.maxPoints - static_cast<int64_t>(rank);
        }
    }

    std::vector<std::unordered_map<std::string, Value>> generated;
    generated.reserve(nameOrder.size());
    for (const auto& name : nameOrder) {
        std::unordered_map<std::string, Value> row;
        row[tab.rankedPoints.nameColumn] = name;
        row[tab.rankedPoints.pointsColumn] = totals[name];
        generated.push_back(std::move(row));
    }
    return generated;
}

static const OutputDef* selectOutput(const GameDef& def, const std::string& outputId) {
    if (!outputId.empty()) {
        auto it = Utils::findIdentifier(def.outputs, outputId);
        if (it != def.outputs.end()) return &it->second;
        return nullptr;
    }

    auto it0 = def.outputs.find("");
    if (it0 != def.outputs.end()) return &it0->second;

    for (const auto& id : def.outputOrder) {
        auto it = Utils::findIdentifier(def.outputs, id);
        if (it != def.outputs.end()) return &it->second;
    }
    return nullptr;
}

static std::string ordinalOutputLabel(const GameDef& def, const OutputDef* out) {
    if (!out) return "";

    int index = 0;
    auto isSelected = [&](const std::string& id) {
        auto it = Utils::findIdentifier(def.outputs, id);
        return it != def.outputs.end() && &it->second == out;
    };

    auto suffix = [](int n) {
        if (n % 100 >= 11 && n % 100 <= 13) return std::string("th");
        switch (n % 10) {
            case 1: return std::string("st");
            case 2: return std::string("nd");
            case 3: return std::string("rd");
            default: return std::string("th");
        }
    };

    if (def.outputs.find("") != def.outputs.end()) {
        ++index;
        if (isSelected("")) return "the " + std::to_string(index) + suffix(index) + " one";
    }

    for (const auto& id : def.outputOrder) {
        if (id.empty()) continue;
        if (Utils::findIdentifier(def.outputs, id) == def.outputs.end()) continue;
        ++index;
        if (isSelected(id)) return "the " + std::to_string(index) + suffix(index) + " one";
    }

    return "the selected one";
}

} // namespace

HiScoreResult ResultRenderer::render(const GameDef& def,
    const std::vector<std::unordered_map<std::string, Value>>& rows,
    const std::string& outputId,
    const ReadOptions& options,
    const TraceSink* trace) {
    HiScoreResult result;
    result.ok = true;

    const OutputDef* out = selectOutput(def, outputId);
    if (!out) return result;

    if (trace) {
        trace->line("TRACE: output selected: " + ordinalOutputLabel(def, out));
        trace->line("TRACE: data displayed into:");
    }

    auto computeCell =
        [&](const Table& tab,
            const Column& col,
            const std::unordered_map<std::string, Value>& row,
            size_t rowIdx) -> Value {
            Value v = std::monostate{};

            const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;

            const std::unordered_map<std::string, Value> emptySourceRow;
            const std::unordered_map<std::string, Value>* sourceRow = &row;
            if (col.sourceRow == SourceRowKind::OutputIndex &&
                !Utils::ieq(src, "index") && !Utils::ieq(src, "unsorted_index")) {
                sourceRow = rowIdx < rows.size() ? &rows[rowIdx] : &emptySourceRow;
            }

            if (Utils::ieq(src, "index")) {
                if (tab.sortKey.empty() && !tab.ignoreRules.empty()) {
                    auto it = row.find(kUnsortedIndexKey);
                    v = (it != row.end()) ? it->second : Value((int64_t)rowIdx);
                }
                else {
                    v = (int64_t)rowIdx;
                }
            }
            else if (Utils::ieq(src, "unsorted_index")) {
                auto it = row.find(kUnsortedIndexKey);
                v = (it != row.end()) ? it->second : Value((int64_t)rowIdx);
            }
            else {
                auto it = Utils::findIdentifier(*sourceRow, src);
                if (it != sourceRow->end()) v = it->second;
            }

            if (!col.format.empty()) {
                v = Formatter::apply(def.formats, col.format,
                    *sourceRow,
                    v,
                    (int)rowIdx);
            }

            return v;
        };

    bool emittedTable = false;
    const size_t missingIndex = (size_t)-1;
    std::vector<size_t> renderedTableIndices(out->tables.size(), missingIndex);
    std::vector<size_t> renderedFieldIndices(out->fields.size(), missingIndex);
    std::vector<std::unordered_map<std::string, Value>> midwayRows;
    if (Utils::ieq(out->sortMethod, "midway")) {
        midwayRows.reserve(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            auto row = rows[i];
            row[kUnsortedIndexKey] = (int64_t)i;
            midwayRows.push_back(std::move(row));
        }
    }

    for (size_t tableDefinitionIndex = 0;
         tableDefinitionIndex < out->tables.size();
         ++tableDefinitionIndex) {
        const auto& tab = out->tables[tableDefinitionIndex];
        if (options.keepFirstTable && emittedTable) break;
        const bool hiddenMidwaySort = Utils::ieq(out->sortMethod, "midway") &&
            Utils::ieq(tab.display, "sort");
        if (!hiddenMidwaySort && !displayAllowed(tab.display, options)) continue;

        bool anyCols = false;
        for (const auto& col : tab.cols) {
            if (optionAllowsColumn(col, options)) {
                anyCols = true;
                break;
            }
        }
        if (!anyCols) continue;

        std::vector<std::unordered_map<std::string, Value>> filtered;
        if (tab.rankedPoints.enabled) {
            filtered = buildRankedPointsRows(tab, rows);
            for (size_t i = 0; i < filtered.size(); ++i)
                filtered[i][kUnsortedIndexKey] = static_cast<int64_t>(i);
        }
        else if (!midwayRows.empty()) {
            filtered = midwayRows;
        }
        else {
            filtered.reserve(rows.size());
            for (size_t i = 0; i < rows.size(); ++i) {
                const auto& r = rows[i];
                if (!rowRelevantToTable(def, tab, r, options)) continue;
                auto rowCopy = r;
                rowCopy[kUnsortedIndexKey] = (int64_t)i;
                filtered.push_back(std::move(rowCopy));
            }
        }

        // Explicit field operands may refer to an element outside a loop.
        // Make only those referenced fields available on every table row.
        if (!tab.rankedPoints.enabled && !rows.empty()) {
            std::unordered_set<std::string> globalOperandIds;
            for (const auto& formatEntry : def.formats) {
                for (const auto& op : formatEntry.second.mathOps) {
                    if (op.hasReference && !op.reference.id.empty()) {
                        globalOperandIds.insert(op.reference.id);
                    }
                }
            }
            for (auto& filteredRow : filtered) {
                for (const auto& id : globalOperandIds) {
                    auto field = Utils::findIdentifier(rows.front(), id);
                    if (field != rows.front().end() &&
                        Utils::findIdentifier(filteredRow, id) == filteredRow.end()) {
                        filteredRow[id] = field->second;
                    }
                }
            }
        }

        if (!tab.sortKey.empty() || !tab.sortKeys.empty()) {
            if (Utils::ieq(out->sortMethod, "midway")) {
                djgppQsort(filtered, [&](const auto& a, const auto& b) {
                    const bool aEligible = rowRelevantToTable(def, tab, a, options) &&
                        !rowShouldIgnore(tab, a, def.formats, 0);
                    const bool bEligible = rowRelevantToTable(def, tab, b, options) &&
                        !rowShouldIgnore(tab, b, def.formats, 0);
                    if (aEligible != bEligible) return aEligible ? -1 : 1;
                    if (!aEligible) return 0;
                    return compareRowsThreeWay(a, b, tab, def.formats);
                });
                midwayRows = filtered;
            }
            else {
                std::stable_sort(filtered.begin(), filtered.end(),
                    [&](const auto& a, const auto& b) { return compareRows(a, b, tab, def.formats); });
            }

            if (!Utils::ieq(out->sortMethod, "midway") && !tab.rankedPoints.enabled &&
                Utils::ieq(tab.sortOrder, "desc") && reverseEqualSortGroups(tab)) {
                auto sortKey = [&](const std::unordered_map<std::string, Value>& r) -> Value {
                    return sortKeyValue(r, tab, def.formats);
                };

                size_t groupStart = 0;
                while (groupStart < filtered.size()) {
                    size_t groupEnd = groupStart + 1;
                    Value key = sortKey(filtered[groupStart]);
                    while (groupEnd < filtered.size() &&
                        compareValueForSort(key, sortKey(filtered[groupEnd])) == 0) {
                        ++groupEnd;
                    }

                    if (groupEnd - groupStart > 1) {
                        std::reverse(filtered.begin() + (ptrdiff_t)groupStart,
                            filtered.begin() + (ptrdiff_t)groupEnd);
                    }
                    groupStart = groupEnd;
                }
            }
        }

        // Midway's leaderboard code sorts the complete profile array, including
        // unused and ineligible records.  Eligibility is applied only while the
        // sorted page is displayed, and the complete order feeds the next page.
        if (Utils::ieq(out->sortMethod, "midway") && !hiddenMidwaySort) {
            filtered.erase(std::remove_if(filtered.begin(), filtered.end(),
                [&](const auto& row) {
                    return !rowRelevantToTable(def, tab, row, options);
                }), filtered.end());
        }

        if (hiddenMidwaySort) continue;

        // A single-column <sum> is used by the official definitions for
        // whole-table totals (for example KOF 2001 win percentages).
        for (const auto& formatEntry : def.formats) {
            const FormatDef& format = formatEntry.second;
            if (format.sumCols.size() != 1) continue;
            const FormatColRef& ref = format.sumCols.front();
            int64_t total = 0;
            for (auto& aggregateRow : filtered) {
                auto valueIt = Utils::findIdentifier(aggregateRow, ref.id);
                if (valueIt == aggregateRow.end()) continue;
                Value aggregateValue = valueIt->second;
                if (!ref.format.empty()) {
                    aggregateValue = Formatter::apply(
                        def.formats, ref.format, aggregateRow, aggregateValue);
                }
                total += Utils::valueToInt(aggregateValue);
            }
            const std::string key = "__hi2txt_global_sum:" + format.id;
            for (auto& aggregateRow : filtered) aggregateRow[key] = total;
        }

        if (!tab.ignoreRules.empty()) {
            std::vector<std::unordered_map<std::string, Value>> kept;
            kept.reserve(filtered.size());
            for (size_t i = 0; i < filtered.size(); ++i) {
                if (rowShouldIgnore(tab, filtered[i], def.formats, i)) continue;
                kept.push_back(std::move(filtered[i]));
            }
            filtered = std::move(kept);
        }

        if (filtered.empty() && !tab.showEmpty) continue;

        HiScoreTable renderedTable;
        renderedTable.id = tab.id;
        renderedTable.display = displayLevelOf(tab.display);

        std::vector<const Column*> selectedColumns;
        selectedColumns.reserve(tab.cols.size());
        for (const auto& col : tab.cols) {
            if (!optionAllowsColumn(col, options)) continue;
            if (options.maxColumns > 0 && (int)selectedColumns.size() >= options.maxColumns) break;
            selectedColumns.push_back(&col);
            renderedTable.columns.push_back(col.headerVisible ? col.id : std::string());
            if (trace) trace->line("TRACE: output table field: " + col.id);
            const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
            renderedTable.columnInfo.push_back(HiScoreColumn{ col.id, src, displayLevelOf(col.display) });
        }

        for (size_t rowIdx = 0; rowIdx < filtered.size(); ++rowIdx) {
            const auto& r = filtered[rowIdx];
            std::unordered_map<std::string, std::string> rowValues;
            for (const auto& col : tab.cols) {
                if (!displayAllowed(col.display, options)) continue;
                Value v = computeCell(tab, col, r, rowIdx);
                rowValues[col.id] = Utils::valueToString(v);
            }

            if (!rowMatchesKeepTableValues(rowValues, options) &&
                !(options.keepFirstScore && rowIdx == 0)) {
                continue;
            }

            std::vector<std::string> renderedRow;
            renderedRow.reserve(renderedTable.columns.size());

            for (const Column* colPtr : selectedColumns) {
                const auto& col = *colPtr;

                auto it = Utils::findIdentifier(rowValues, col.id);
                std::string cell = it == rowValues.end() ? std::string() : it->second;
                if (Utils::ieq(col.id, "SCORE")) {
                    cell = groupScoreValue(cell, options);
                }
                renderedRow.push_back(std::move(cell));
            }

            renderedTable.rows.push_back(std::move(renderedRow));
            const int effectiveMaxLines = options.maxLines > 0 ? options.maxLines : tab.linesMax;
            if (effectiveMaxLines > 0 && (int)renderedTable.rows.size() >= effectiveMaxLines) break;
        }

        if (!renderedTable.rows.empty() || tab.showEmpty) {
            renderedTableIndices[tableDefinitionIndex] = result.tables.size();
            result.tables.push_back(std::move(renderedTable));
            emittedTable = true;
        }
    }

    for (size_t fieldDefinitionIndex = 0;
         fieldDefinitionIndex < out->fields.size();
         ++fieldDefinitionIndex) {
        const auto& f = out->fields[fieldDefinitionIndex];
        if (!displayAllowed(f.display, options)) continue;
        if (!options.keepFields.empty() && !containsCi(options.keepFields, f.id)) continue;
        if (containsCi(options.hideFields, f.id)) continue;
        if (trace) trace->line("TRACE: output field: " + f.id);

        const std::unordered_map<std::string, Value> emptyRow;
        const auto& row0 = rows.empty() ? emptyRow : rows[0];

        Value v = std::monostate{};
        const std::string src = Utils::trim(f.src).empty() ? f.id : f.src;

        if (Utils::ieq(src, "index")) {
            v = (int64_t)0;
        }
        else {
            auto it = Utils::findIdentifier(row0, src);
            if (it != row0.end()) v = it->second;
        }

        const bool sourcePresent =
            Utils::ieq(src, "index") || Utils::findIdentifier(row0, src) != row0.end();
        if (!sourcePresent) {
            bool synthesizesFromRow = false;
            const auto dependencies = collectFormatDeps(def.formats, f.format);
            for (const auto& dependency : dependencies) {
                if (Utils::findIdentifier(row0, dependency) != row0.end()) {
                    synthesizesFromRow = true;
                    break;
                }
            }
            if (!synthesizesFromRow) continue;
        }

        if (!f.format.empty()) {
            auto tmp = row0;
            v = Formatter::apply(def.formats, f.format, tmp, v, 0);
        }

        std::string value = Utils::valueToString(v);
        if (Utils::ieq(f.id, "SCORE")) {
            value = groupScoreValue(value, options);
        }
        // Official hi2txt traces an enabled standalone field but does not
        // serialize it when formatting leaves an empty value.
        if (value.empty()) continue;

        renderedFieldIndices[fieldDefinitionIndex] = result.fields.size();
        result.fields.push_back(HiScoreField{
            f.id,
            value,
            src,
            displayLevelOf(f.display)
        });
    }

    for (const auto& item : out->items) {
        if (item.kind == OutputItemKind::Table) {
            if (item.index >= renderedTableIndices.size()) continue;
            const size_t renderedIndex = renderedTableIndices[item.index];
            if (renderedIndex == missingIndex) continue;
            result.outputOrder.push_back(
                HiScoreOutputItem{ HiScoreOutputKind::Table, renderedIndex });
        }
        else {
            if (item.index >= renderedFieldIndices.size()) continue;
            const size_t renderedIndex = renderedFieldIndices[item.index];
            if (renderedIndex == missingIndex) continue;
            result.outputOrder.push_back(
                HiScoreOutputItem{ HiScoreOutputKind::Field, renderedIndex });
        }
    }

    return result;
}

} // namespace openhi2txt
