#include "timetable/domain/assignment/run.hpp"

#include <mathfp/core/fp.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<AssignmentOutput> run_timetable_assignment(
        const AssignmentInput& input
    ) {
        return
            run_timetable_assignment_pipeline(input)
            | mathfp::fp::pipe::and_then(build_assignment_output);
    }

}  // namespace timetable::domain::assignment
