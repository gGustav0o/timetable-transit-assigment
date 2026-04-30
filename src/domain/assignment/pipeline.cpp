#include "timetable/domain/assignment/pipeline.hpp"

#include <utility>

#include "detail/pipeline_internal.hpp"

namespace timetable::domain::assignment {
    mathfp::Expected<AssignmentPipelineResult> run_timetable_assignment_pipeline(
        AssignmentInput input
    ) {
        return detail::run_timetable_assignment_pipeline_with_context(std::move(input));
    }

}  // namespace timetable::domain::assignment
