#pragma once

#include <optional>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/runtime/batch_context.hpp"

namespace timetable::domain::assignment::runtime {

    struct SearchContinuationProjection final {
        ActiveIndexSet active_tasks{};
        ActiveIndexSet active_targets{};
    };

    [[nodiscard]] mathfp::Expected<std::optional<SearchContinuationProjection>>
    filter_continuation_projection(
          SearchBatchContext&   context
        , const SearchBranch&   candidate
        , const ActiveIndexSet& active_tasks
        , const ActiveIndexSet& active_targets
    );

}  // namespace timetable::domain::assignment::runtime
