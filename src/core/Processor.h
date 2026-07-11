#pragma once

#include "core/Types.h"

#include <unordered_map>
#include <vector>

namespace openhi2txt {

struct TraceSink;

class Processor {
public:
    // Check whether a structure's <check> matches this raw buffer (size + definitions).
    static bool checkMatches(const std::vector<uint8_t>& raw, const Structure& s);

    // Decode the selected structure into a set of logical table rows.
    // - Applies per-element table-index rules to decide which row an element writes to.
    // - Implements loop start/step/count and skip-first/skip-last semantics.
    // - Expects any structure-level byte-swap to have been applied to 'raw' already.
    static std::vector<std::unordered_map<std::string, Value>> extractRows(
        const std::vector<uint8_t>& raw,
        const Structure& s,
        const GameDef& def,
        const TraceSink* trace = nullptr);
};

} // namespace openhi2txt
