#include "core/InputPlanner.h"

#include "io/Utils.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace openhi2txt {
namespace {

struct Range {
    uint64_t begin = 0;
    uint64_t end = 0;
};

struct Occurrence {
    const Elt* elt = nullptr;
    Range range;
    size_t order = 0;
};

static std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

static void addId(std::unordered_set<std::string>& ids, const std::string& id) {
    const std::string trimmed = Utils::trim(id);
    if (!trimmed.empty() && !Utils::ieq(trimmed, "index") &&
        !Utils::ieq(trimmed, "unsorted_index")) {
        ids.insert(lower(trimmed));
    }
}

static bool idMatches(const std::string& elementId,
                      const std::unordered_set<std::string>& wanted) {
    const std::string id = lower(Utils::trim(elementId));
    if (wanted.find(id) != wanted.end()) return true;
    for (const auto& dep : wanted) {
        if (id.size() > dep.size() && id.compare(id.size() - dep.size(), dep.size(), dep) == 0 &&
            id[id.size() - dep.size() - 1] == ' ') return true;
        if (dep.size() > id.size() && dep.compare(dep.size() - id.size(), id.size(), id) == 0 &&
            dep[dep.size() - id.size() - 1] == ' ') return true;
    }
    return false;
}

static std::vector<std::string> splitFormatChain(const std::string& chain) {
    std::vector<std::string> result;
    std::stringstream stream(chain);
    std::string token;
    while (std::getline(stream, token, ';')) {
        token = Utils::trim(token);
        if (!token.empty()) result.push_back(token);
    }
    return result;
}

static void collectFormat(const GameDef& def,
                          const std::string& chain,
                          std::unordered_set<std::string>& ids,
                          std::unordered_set<std::string>& visited) {
    for (const auto& token : splitFormatChain(chain)) {
        auto found = Utils::findIdentifier(def.formats, token);
        if (found == def.formats.end()) continue;
        const std::string key = lower(found->first);
        if (!visited.insert(key).second) continue;
        const FormatDef& format = found->second;

        auto collectRef = [&](const FormatColRef& ref) {
            addId(ids, ref.id);
            collectFormat(def, ref.format, ids, visited);
        };
        auto collectPart = [&](const ConcatPart& part) {
            if (part.kind == ConcatPartKind::Column) addId(ids, part.id);
            collectFormat(def, part.format, ids, visited);
        };

        for (const auto& op : format.mathOps) {
            if (op.hasReference) collectRef(op.reference);
        }
        for (const auto& ref : format.sumCols) collectRef(ref);
        for (const auto& ref : format.minCols) collectRef(ref);
        for (const auto& ref : format.maxCols) collectRef(ref);
        for (const auto& part : format.concatParts) collectPart(part);
        for (const auto& affix : format.prefixes)
            for (const auto& part : affix.parts) collectPart(part);
        for (const auto& affix : format.suffixes)
            for (const auto& part : affix.parts) collectPart(part);
        for (const auto& map : format.cases) {
            collectFormat(def, map.operatorFormat, ids, visited);
            collectFormat(def, map.format, ids, visited);
        }
        for (const auto& id : format.referencedColumns) addId(ids, id);
    }
}

static const OutputDef* selectOutput(const GameDef& def, const std::string& outputId) {
    auto found = Utils::findIdentifier(def.outputs, outputId);
    if (found != def.outputs.end()) return &found->second;
    if (!outputId.empty()) return nullptr;
    for (const auto& id : def.outputOrder) {
        found = Utils::findIdentifier(def.outputs, id);
        if (found != def.outputs.end()) return &found->second;
    }
    return nullptr;
}

static void collectOutputDependencies(const GameDef& def,
                                      const Structure& structure,
                                      std::unordered_set<std::string>& ids) {
    const OutputDef* output = selectOutput(def, structure.outputId);
    if (!output) return;
    std::unordered_set<std::string> formats;

    auto collectColumn = [&](const Column& col) {
        addId(ids, Utils::trim(col.src).empty() ? col.id : col.src);
        collectFormat(def, col.format, ids, formats);
    };
    for (const auto& field : output->fields) collectColumn(field);
    for (const auto& table : output->tables) {
        for (const auto& col : table.cols) collectColumn(col);

        auto collectTableReference = [&](const std::string& reference,
                                         const std::string& format) {
            bool matchedColumn = false;
            for (const auto& col : table.cols) {
                if (!Utils::ieq(col.id, reference)) continue;
                collectColumn(col);
                matchedColumn = true;
            }
            if (!matchedColumn) addId(ids, reference);
            collectFormat(def, format, ids, formats);
        };

        for (const auto& rule : table.ignoreRules) collectTableReference(rule.colId, "");
        for (const auto& key : table.sortKeys) collectTableReference(key.src, key.format);
        if (!table.sortKey.empty())
            collectTableReference(table.sortKey, table.sortFormat);
        if (table.rankedPoints.enabled) {
            addId(ids, table.rankedPoints.nameColumn);
            for (const auto& source : table.rankedPoints.sources) addId(ids, source.src);
        }
    }
}

static bool canInvalidateWholeDecode(const Elt& elt) {
    if (!Utils::ieq(elt.type, "int")) return false;
    return elt.intBase == IntBaseKind::BcdBE || elt.intBase == IntBaseKind::BcdLE ||
        Utils::ieq(Utils::trim(elt.decodingProfile), "bcd") ||
        Utils::ieq(Utils::trim(elt.decodingProfile), "bcd-le");
}

static uint64_t addClamped(uint64_t a, uint64_t b) {
    if (b > std::numeric_limits<uint64_t>::max() - a)
        return std::numeric_limits<uint64_t>::max();
    return a + b;
}

static void flatten(const Structure& structure,
                    std::vector<Occurrence>& occurrences,
                    std::vector<Range>& stoppedLoopTails,
                    uint64_t& maximumExtent) {
    uint64_t cursor = 0;
    size_t order = 0;
    for (const auto& item : structure.items) {
        if (item.kind == StructureItem::Kind::Elt) {
            const Elt& elt = item.elt;
            const uint64_t size = static_cast<uint64_t>(std::max(0, elt.size));
            const uint64_t pos = elt.offset >= 0 ? static_cast<uint64_t>(elt.offset) : cursor;
            occurrences.push_back({&elt, {pos, addClamped(pos, size)}, order++});
            cursor = std::max(cursor, addClamped(pos, size));
            maximumExtent = std::max(maximumExtent, cursor);
            continue;
        }

        const Loop& loop = item.loop;
        const uint64_t loopBegin = cursor;
        int fullSize = 0;
        for (const auto& elt : loop.elts) fullSize += std::max(0, elt.size);
        const int count = std::max(0, loop.count);
        uint64_t loopCursor = cursor;
        for (int iteration = 0; iteration < count; ++iteration) {
            const bool last = iteration == count - 1;
            const int limit = std::max(0, last
                ? fullSize - std::max(0, loop.skipLastBytes)
                : fullSize);
            const int prefix = iteration == 0 ? std::max(0, loop.skipFirstBytes) : 0;
            const uint64_t base = loopCursor;
            int consumed = 0;
            for (const auto& elt : loop.elts) {
                const int size = std::max(0, elt.size);
                const int logicalBegin = consumed;
                const int logicalEnd = consumed + size;
                consumed += size;
                if (size == 0 || logicalEnd <= prefix) continue;
                if (logicalBegin >= limit) break;
                const int readBegin = std::max(logicalBegin, prefix);
                const int readEnd = std::min(logicalEnd, limit);
                if (readEnd <= readBegin) continue;
                const uint64_t pos = elt.offset >= 0
                    ? static_cast<uint64_t>(elt.offset)
                    : addClamped(base, static_cast<uint64_t>(readBegin - prefix));
                occurrences.push_back({&elt,
                    {pos, addClamped(pos, static_cast<uint64_t>(readEnd - readBegin))}, order++});
                maximumExtent = std::max(maximumExtent, occurrences.back().range.end);
            }
            loopCursor = addClamped(base, static_cast<uint64_t>(std::max(0, limit - prefix)));
            maximumExtent = std::max(maximumExtent, loopCursor);
        }
        cursor = std::max(cursor, loopCursor);
        if (loop.hasStopCondition) stoppedLoopTails.push_back({loopBegin, 0});
    }
    for (auto& range : stoppedLoopTails) range.end = maximumExtent;
}

static void mergeRanges(std::vector<Range>& ranges) {
    ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
        [](const Range& range) { return range.end <= range.begin; }), ranges.end());
    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) {
        return a.begin < b.begin || (a.begin == b.begin && a.end < b.end);
    });
    std::vector<Range> merged;
    for (const auto& range : ranges) {
        if (merged.empty() || range.begin > merged.back().end) merged.push_back(range);
        else merged.back().end = std::max(merged.back().end, range.end);
    }
    ranges.swap(merged);
}

static void expandForStructureSwap(std::vector<Range>& ranges, int byteSwap,
                                   uint64_t exactSize) {
    if (byteSwap <= 1) return;
    const uint64_t chunk = static_cast<uint64_t>(byteSwap);
    for (auto& range : ranges) {
        range.begin = (range.begin / chunk) * chunk;
        range.end = addClamped(((range.end - 1) / chunk) * chunk, chunk);
        if (exactSize != 0) range.end = std::min(range.end, exactSize);
    }
}

static HiScoreInputPlan planStructure(const GameDef& def, const Structure& structure,
                                      size_t structureIndex) {
    HiScoreInputPlan plan;
    plan.structureIndex = structureIndex;
    plan.fileKind = Utils::trim(structure.fileKind.empty() ? ".hi" : structure.fileKind);
    if (Utils::ieq(plan.fileKind, "hi")) plan.fileKind = ".hi";
    plan.outputId = structure.outputId;
    plan.sourceWindowOffset = structure.hasInputWindow ? structure.inputOffset : 0;
    plan.sourceWindowLength = structure.hasInputWindow ? structure.inputLength : 0;
    for (int size : structure.checkSizes)
        if (size > 0) plan.acceptedBufferSizes.push_back(static_cast<uint64_t>(size));

    std::unordered_set<std::string> wanted;
    collectOutputDependencies(def, structure, wanted);
    for (const auto& item : structure.items) {
        if (item.kind == StructureItem::Kind::Loop && item.loop.hasStopCondition)
            addId(wanted, item.loop.stopFieldId);
    }

    std::vector<Occurrence> occurrences;
    std::vector<Range> stoppedLoopTails;
    uint64_t maximumExtent = 0;
    flatten(structure, occurrences, stoppedLoopTails, maximumExtent);

    // BCD validation is performed while extracting every element, including
    // elements not rendered by the selected output. A change can therefore
    // turn an otherwise valid complete snapshot into a decode error.
    for (const auto& occurrence : occurrences) {
        if (canInvalidateWholeDecode(*occurrence.elt)) addId(wanted, occurrence.elt->id);
    }

    // Element formats and table-index references can make additional elements live.
    std::unordered_set<std::string> visitedFormats;
    bool changed = true;
    std::unordered_set<size_t> live;
    while (changed) {
        changed = false;
        for (const auto& occurrence : occurrences) {
            if (!idMatches(occurrence.elt->id, wanted)) continue;
            if (live.insert(occurrence.order).second) changed = true;
            const size_t before = wanted.size();
            collectFormat(def, occurrence.elt->format, wanted, visitedFormats);
            if (occurrence.elt->tableIndexKind == TableIndexKind::IndexFromValue ||
                occurrence.elt->tableIndexKind == TableIndexKind::ValueFromIndex) {
                addId(wanted, occurrence.elt->tableIndexCol);
                collectFormat(def, occurrence.elt->tableIndexFormat, wanted, visitedFormats);
            }
            if (occurrence.elt->tableIndexKind == TableIndexKind::Last) {
                for (const auto& prior : occurrences) {
                    if (prior.order >= occurrence.order) break;
                    addId(wanted, prior.elt->id);
                }
            }
            if (wanted.size() != before) changed = true;
        }
    }

    std::vector<Range> ranges;
    for (const auto& occurrence : occurrences)
        if (live.find(occurrence.order) != live.end()) ranges.push_back(occurrence.range);
    ranges.insert(ranges.end(), stoppedLoopTails.begin(), stoppedLoopTails.end());
    for (const auto& check : structure.checkAll) {
        if (check.offset >= 0) ranges.push_back({static_cast<uint64_t>(check.offset),
            addClamped(static_cast<uint64_t>(check.offset), check.bytes.size())});
    }
    for (const auto& check : structure.checkAny) {
        if (check.offset >= 0) ranges.push_back({static_cast<uint64_t>(check.offset),
            addClamped(static_cast<uint64_t>(check.offset), check.bytes.size())});
    }
    for (const auto& region : structure.decodeRegions) {
        ranges.push_back({region.offset, addClamped(region.offset, addClamped(region.size, 2))});
        maximumExtent = std::max(maximumExtent,
            addClamped(region.offset, addClamped(region.size, 10)));
    }

    uint64_t exactSize = structure.hasInputWindow ? structure.inputLength : 0;
    if (exactSize == 0 && plan.acceptedBufferSizes.size() == 1)
        exactSize = plan.acceptedBufferSizes.front();
    expandForStructureSwap(ranges, structure.byteSwap, exactSize);
    mergeRanges(ranges);

    for (const auto& range : ranges) {
        plan.watchRanges.push_back({
            addClamped(plan.sourceWindowOffset, range.begin),
            range.end - range.begin
        });
    }
    return plan;
}

} // namespace

std::vector<HiScoreInputPlan> InputPlanner::plan(const GameDef& def) {
    std::vector<HiScoreInputPlan> result;
    result.reserve(def.structures.size());
    for (size_t i = 0; i < def.structures.size(); ++i)
        result.push_back(planStructure(def, def.structures[i], i));
    return result;
}

} // namespace openhi2txt
