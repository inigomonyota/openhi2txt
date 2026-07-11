#include "core/ResultRenderer.h"
#include "core/Formatter.h"
#include "core/Trace.h"
#include "io/Utils.h"

#include <algorithm>
#include <cstddef>
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
    if (c0 == '+' || c0 == '-' || c0 == '*' || c0 == '/' || c0 == 'x' || c0 == 'X') return true;
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
    auto it = fmts.find(fmtId);
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
                if (fmts.find(tok) != fmts.end()) {
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
            if (fmts.find(tok) != fmts.end()) {
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
        if (fmts.find(tok) != fmts.end()) {
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
    bool hasNonIndexVisible = false;
    for (const auto& col : tab.cols) {
        if (!displayAllowed(col.display, options)) continue;
        const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
        if (!Utils::ieq(src, "index") && !Utils::ieq(src, "unsorted_index")) {
            hasNonIndexVisible = true;
            break;
        }
    }
    if (!hasNonIndexVisible) return true;

    auto colHasData = [&](const Column& col) -> bool {
        const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;

        if (!src.empty() && !Utils::ieq(src, "index") && !Utils::ieq(src, "unsorted_index")) {
            auto it = row.find(src);
            if (it != row.end() && !valueEmpty(it->second)) return true;
        }

        if (!col.format.empty()) {
            auto deps = collectFormatDeps(def.formats, col.format);
            for (const auto& dep : deps) {
                auto it2 = row.find(dep);
                if (it2 != row.end() && !valueEmpty(it2->second)) return true;
            }
        }

        return false;
    };

    for (const auto& col : tab.cols) {
        if (!displayAllowed(col.display, options)) continue;
        const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
        if (Utils::ieq(src, "index") || Utils::ieq(src, "unsorted_index")) continue;
        if (!colHasData(col)) return false;
        break;
    }

    for (const auto& col : tab.cols) {
        if (!displayAllowed(col.display, options)) continue;
        const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
        if (Utils::ieq(src, "index") || Utils::ieq(src, "unsorted_index")) continue;
        if (colHasData(col)) return true;
    }
    return false;
}

static bool rowShouldIgnore(const Table& tab,
    const std::unordered_map<std::string, Value>& row,
    const std::unordered_map<std::string, FormatDef>& formats,
    size_t rowIdx) {
    if (tab.ignoreRules.empty()) return false;

    auto matchRule = [&](const IgnoreRule& r) -> bool {
        auto it = row.find(r.colId);
        Value v = (it != row.end()) ? it->second : Value{};

        if (it == row.end()) {
            for (const auto& col : tab.cols) {
                if (!Utils::ieq(col.id, r.colId)) continue;

                const std::string src = Utils::trim(col.src).empty() ? col.id : col.src;
                if (Utils::ieq(src, "index")) {
                    v = (int64_t)rowIdx;
                }
                else if (Utils::ieq(src, "unsorted_index")) {
                    auto ui = row.find(kUnsortedIndexKey);
                    v = (ui != row.end()) ? ui->second : Value((int64_t)rowIdx);
                }
                else {
                    auto srcIt = row.find(src);
                    if (srcIt != row.end()) v = srcIt->second;
                }

                if (!col.format.empty()) {
                    v = Formatter::apply(formats, col.format,
                        const_cast<std::unordered_map<std::string, Value>&>(row),
                        v,
                        (int)rowIdx);
                }
                break;
            }
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
    const std::unordered_map<std::string, FormatDef>& formats) {
    if (Utils::ieq(tab.sortKey, "index")) return (int64_t)0;

    const Column* sortCol = nullptr;
    for (const auto& col : tab.cols) {
        if (Utils::ieq(col.id, tab.sortKey)) {
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
            auto it = row.find(src);
            if (it != row.end()) {
                v = it->second;
                fromColumnAlias = true;
            }
        }
    }

    if (!fromColumnAlias) {
        auto it = row.find(tab.sortKey);
        if (it != row.end()) v = it->second;
    }

    if (!tab.sortFormat.empty()) {
        v = Formatter::apply(formats, tab.sortFormat,
            const_cast<std::unordered_map<std::string, Value>&>(row),
            v);
    }

    return v;
}

static bool compareRows(const std::unordered_map<std::string, Value>& a,
    const std::unordered_map<std::string, Value>& b,
    const Table& tab,
    const std::unordered_map<std::string, FormatDef>& formats) {
    const bool asc = !Utils::ieq(tab.sortOrder, "desc");
    Value ka = sortKeyValue(a, tab, formats);
    Value kb = sortKeyValue(b, tab, formats);

    const int cmp = compareValueForSort(ka, kb);
    return asc ? (cmp < 0) : (cmp > 0);
}

static bool reverseEqualSortGroups(const Table& tab) {
    return tab.sortKey.find('_') == std::string::npos;
}

static const OutputDef* selectOutput(const GameDef& def, const std::string& outputId) {
    if (!outputId.empty()) {
        auto it = def.outputs.find(outputId);
        if (it != def.outputs.end()) return &it->second;
        return nullptr;
    }

    auto it0 = def.outputs.find("");
    if (it0 != def.outputs.end()) return &it0->second;

    for (const auto& id : def.outputOrder) {
        auto it = def.outputs.find(id);
        if (it != def.outputs.end()) return &it->second;
    }
    return nullptr;
}

static std::string ordinalOutputLabel(const GameDef& def, const OutputDef* out) {
    if (!out) return "";

    int index = 0;
    auto isSelected = [&](const std::string& id) {
        auto it = def.outputs.find(id);
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
        if (def.outputs.find(id) == def.outputs.end()) continue;
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
                auto it = row.find(src);
                if (it != row.end()) v = it->second;
            }

            if (!col.format.empty()) {
                v = Formatter::apply(def.formats, col.format,
                    const_cast<std::unordered_map<std::string, Value>&>(row),
                    v,
                    (int)rowIdx);
            }

            return v;
        };

    bool emittedTable = false;
    for (const auto& tab : out->tables) {
        if (options.keepFirstTable && emittedTable) break;
        if (!displayAllowed(tab.display, options)) continue;

        bool anyCols = false;
        for (const auto& col : tab.cols) {
            if (optionAllowsColumn(col, options)) {
                anyCols = true;
                break;
            }
        }
        if (!anyCols) continue;

        std::vector<std::unordered_map<std::string, Value>> filtered;
        filtered.reserve(rows.size());
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            if (!rowRelevantToTable(def, tab, r, options)) continue;
            auto rowCopy = r;
            rowCopy[kUnsortedIndexKey] = (int64_t)i;
            filtered.push_back(std::move(rowCopy));
        }

        if (!tab.sortKey.empty()) {
            std::stable_sort(filtered.begin(), filtered.end(),
                [&](const auto& a, const auto& b) { return compareRows(a, b, tab, def.formats); });

            if (Utils::ieq(tab.sortOrder, "desc") && reverseEqualSortGroups(tab)) {
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

        if (!tab.ignoreRules.empty()) {
            std::vector<std::unordered_map<std::string, Value>> kept;
            kept.reserve(filtered.size());
            for (size_t i = 0; i < filtered.size(); ++i) {
                if (rowShouldIgnore(tab, filtered[i], def.formats, i)) continue;
                kept.push_back(std::move(filtered[i]));
            }
            filtered = std::move(kept);
        }

        if (filtered.empty()) continue;

        HiScoreTable renderedTable;
        renderedTable.id = tab.id;
        renderedTable.display = displayLevelOf(tab.display);

        std::vector<const Column*> selectedColumns;
        selectedColumns.reserve(tab.cols.size());
        for (const auto& col : tab.cols) {
            if (!optionAllowsColumn(col, options)) continue;
            if (options.maxColumns > 0 && (int)selectedColumns.size() >= options.maxColumns) break;
            selectedColumns.push_back(&col);
            renderedTable.columns.push_back(col.id);
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

                auto it = rowValues.find(col.id);
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

        if (!renderedTable.rows.empty()) {
            result.tables.push_back(std::move(renderedTable));
            emittedTable = true;
        }
    }

    for (const auto& f : out->fields) {
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
            auto it = row0.find(src);
            if (it != row0.end()) v = it->second;
        }

        if (!f.format.empty()) {
            auto tmp = row0;
            v = Formatter::apply(def.formats, f.format, tmp, v, 0);
        }

        std::string value = Utils::valueToString(v);
        if (Utils::ieq(f.id, "SCORE")) {
            value = groupScoreValue(value, options);
        }

        result.fields.push_back(HiScoreField{
            f.id,
            value,
            src,
            displayLevelOf(f.display)
        });
    }

    return result;
}

} // namespace openhi2txt
