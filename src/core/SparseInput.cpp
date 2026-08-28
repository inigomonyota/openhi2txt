#include "core/SparseInput.h"

#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace openhi2txt {

SparseInputResult SparseInput::materialize(const std::vector<HiScoreSparseInput>& sparseInputs) {
    SparseInputResult result;
    result.inputs.reserve(sparseInputs.size());

    for (const auto& sparse : sparseInputs) {
        if (sparse.sourceSize == 0) {
            result.error = "Sparse input source size must be greater than zero";
            return result;
        }
        if (sparse.sourceSize > (std::numeric_limits<std::size_t>::max)()) {
            result.error = "Sparse input source is too large to materialize";
            return result;
        }
        if (sparse.sourceOffset > (std::numeric_limits<std::uint64_t>::max)() - sparse.sourceSize) {
            result.error = "Sparse input source window overflows its logical address space";
            return result;
        }
        if (sparse.ranges.empty()) {
            result.error = "Sparse input contains no ranges";
            return result;
        }

        std::vector<const HiScoreInputRange*> ordered;
        ordered.reserve(sparse.ranges.size());
        for (const auto& range : sparse.ranges) ordered.push_back(&range);
        std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
            return left->offset < right->offset;
        });

        const std::uint64_t windowEnd = sparse.sourceOffset + sparse.sourceSize;
        std::uint64_t previousEnd = sparse.sourceOffset;
        for (const auto* range : ordered) {
            if (range->bytes.empty()) {
                result.error = "Sparse input contains an empty range";
                return result;
            }
            const std::uint64_t length = range->bytes.size();
            if (range->offset < sparse.sourceOffset || range->offset > windowEnd ||
                length > windowEnd - range->offset) {
                result.error = "Sparse input range lies outside its source window";
                return result;
            }
            if (range->offset < previousEnd) {
                result.error = "Sparse input ranges overlap";
                return result;
            }
            previousEnd = range->offset + length;
        }

        HiScoreInput materialized;
        materialized.fileKind = sparse.fileKind;
        materialized.sourceName = sparse.sourceName;
        if (sparse.sourceSize > materialized.bytes.max_size()) {
            result.error = "Sparse input source is too large to materialize";
            return result;
        }
        try {
            materialized.bytes.assign(static_cast<std::size_t>(sparse.sourceSize), 0);
        }
        catch (const std::bad_alloc&) {
            result.error = "Unable to allocate sparse input source buffer";
            return result;
        }

        for (const auto* range : ordered) {
            const auto destination = materialized.bytes.begin() +
                static_cast<std::size_t>(range->offset - sparse.sourceOffset);
            std::copy(range->bytes.begin(), range->bytes.end(), destination);
        }
        result.inputs.push_back(std::move(materialized));
    }

    result.ok = true;
    return result;
}

} // namespace openhi2txt
