#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/config/runtime_override_policy.hpp"
#include "timetable/domain/assignment.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<AssignmentInput> apply_runtime_override_policy(
          AssignmentInput                          input
        , const timetable::config::RuntimeOverridePolicy& policy
    );

}  // namespace timetable::domain::assignment
