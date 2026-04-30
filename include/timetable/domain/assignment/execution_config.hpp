#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    /**
     * @brief Runtime execution switches for the assignment pipeline.
     *
     * This configuration controls which top-level calculation stages are
     * executed. It is intentionally separate from SearchParams because it does
     * not define impedance, choice or split mathematics.
     */
    struct AssignmentExecutionConfig final {
        bool calculate_assignment{ true };
    };

    mathfp::Expected<mathfp::Unit> validate_assignment_execution_config(
        const AssignmentExecutionConfig& config
    );

}  // namespace timetable::domain::assignment
