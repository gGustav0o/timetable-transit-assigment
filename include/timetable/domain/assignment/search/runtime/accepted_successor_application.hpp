#pragma once

#include <cstddef>
#include <optional>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/runtime/batch_context.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment::runtime {

    enum class AcceptedSuccessorApplicationOutcome {
          CompletedProjection
        , Rejected
        , Enqueued
    };

    struct AcceptedSuccessorApplicationResult final {
        AcceptedSuccessorApplicationOutcome outcome{
            AcceptedSuccessorApplicationOutcome::Rejected
        };
        std::optional<std::size_t> enqueued_branch_index{};
    };

    [[nodiscard]] mathfp::Expected<AcceptedSuccessorApplicationResult>
    apply_accepted_successor(
          SearchBatchContext&   context
        , const SearchBranch&   parent_branch
        , const SearchSuccessor& successor
        , const ConnectionSegment& connection
        , SearchBranch          candidate
        , const ActiveIndexSet& active_tasks
        , const ActiveIndexSet& active_targets
    );

}  // namespace timetable::domain::assignment::runtime
