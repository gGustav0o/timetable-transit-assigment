#include "timetable/domain/assignment/output.hpp"

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<AssignmentOutput> build_assignment_output(
        const DemandSplitResult& result
    ) {
        (void)result;
        return mathfp::unexpected(
            mathfp::not_implemented("assignment output mapping not implemented yet")
        );
    }

}  // namespace timetable::domain::assignment
