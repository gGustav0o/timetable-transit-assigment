#include "timetable/domain/assignment/validation.hpp"

#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_complete_connection_dominance_config(
        const CompleteConnectionDominanceConfig&
    ) {
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
