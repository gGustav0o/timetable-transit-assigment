#include "timetable/domain/assignment/pipeline.hpp"

#include <utility>

#include <mathfp/core/fp.hpp>

#include "detail/pipeline_internal.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<DemandSplitResult> run_timetable_assignment_pipeline(
        AssignmentInput input
    ) {
        return
            detail::run_timetable_assignment_pipeline_with_context(std::move(input))
            | mathfp::fp::pipe::map([](detail::AssignmentPipelineResult result) {
                return std::move(result.split);
            });
    }

}  // namespace timetable::domain::assignment
