#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/output.hpp"
#include "timetable/domain/assignment/pipeline.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Run the full assignment pipeline and map it to AssignmentOutput.
     */
    mathfp::Expected<AssignmentOutput> run_timetable_assignment(
        const AssignmentInput& input
    );

}  // namespace timetable::domain::assignment
