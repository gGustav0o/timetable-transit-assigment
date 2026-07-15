#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/runtime/batch_context.hpp"

namespace timetable::domain::assignment::runtime {

    enum class ProjectionApplicationOutcome {
          ContinueSearch
        , CompletedProjection
        , RejectedZoneSink
    };

    struct ProjectionApplicationResult final {
        ProjectionApplicationOutcome outcome{
            ProjectionApplicationOutcome::ContinueSearch
        };
    };

    [[nodiscard]] mathfp::Expected<ProjectionApplicationResult>
    apply_projection_sink(
          SearchBatchContext&    context
        , const SearchBranch&    candidate
        , const SearchSuccessor& successor
        , const ActiveIndexSet&  active_tasks
        , const ActiveIndexSet&  active_targets
    );

}  // namespace timetable::domain::assignment::runtime
