#pragma once

#include <cstdint>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/capacity_aware_assignment.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_search_mode(
        CapacityAwareSearchMode mode
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_iteration_config(
        const CapacityIterationConfig& config
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_config(
        const CapacityAwareAssignmentConfig& config
    );

    [[nodiscard]] mathfp::Expected<CapacityIterationConfig> make_capacity_iteration_config(
          std::int32_t max_iterations
        , double       absolute_load_tolerance
        , double       relative_load_tolerance
    );

    [[nodiscard]] mathfp::Expected<CapacityAwareAssignmentConfig> make_capacity_aware_assignment_config(
          bool                    capacity_aware_split_enabled
        , CapacityAwareSearchMode search_mode
        , CapacityPenaltyPolicy   penalty_policy
        , CapacityIterationConfig iteration
    );

    [[nodiscard]] CapacityAwareAssignmentConfig make_capacity_aware_assignment_disabled_config();

}  // namespace timetable::domain::assignment
