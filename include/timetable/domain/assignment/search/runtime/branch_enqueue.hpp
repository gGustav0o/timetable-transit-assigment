#pragma once

#include <cstddef>

#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/runtime/batch_context.hpp"
#include "timetable/domain/assignment/search/runtime/continuation_filter.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment::runtime {

    [[nodiscard]] std::size_t enqueue_od_day_branch(
          SearchBatchContext&    context
        , SearchBranch           candidate
        , const SearchSuccessor& successor
    );

    [[nodiscard]] std::size_t enqueue_projected_branch(
          SearchBatchContext&              context
        , const SearchBranch&              parent_branch
        , const ConnectionSegment&         connection
        , SearchBranch                     candidate
        , const SearchSuccessor&           successor
        , SearchContinuationProjection     projection
    );

}  // namespace timetable::domain::assignment::runtime
