#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment {
    /**
     * @brief Run the full timetable assignment pipeline.
     *
     * Order: preprocessing -> connection search -> connection choice -> demand split.
     */
    mathfp::Expected<DemandSplitResult> run_timetable_assignment_pipeline(
        AssignmentInput input
    );

}  // namespace timetable::domain::assignment
