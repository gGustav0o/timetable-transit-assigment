#include "timetable/domain/assignment/execution_config.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_assignment_execution_config(
        const AssignmentExecutionConfig&
    ) {
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
