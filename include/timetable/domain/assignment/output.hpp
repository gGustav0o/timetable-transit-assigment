#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/steps.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Convert split results to the public assignment output.
     */
    mathfp::Expected<AssignmentOutput> build_assignment_output(
        const DemandSplitResult& result
    );

}  // namespace timetable::domain::assignment
