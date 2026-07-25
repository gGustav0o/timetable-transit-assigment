#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/capacity_aware_assignment.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_split_diagnostics(
        const CapacityAwareSplitDiagnostics& diagnostics
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_diagnostics(
        const CapacityAwareAssignmentDiagnostics& diagnostics
    );

    [[nodiscard]] mathfp::Expected<CapacityAwareAssignmentDiagnostics> make_capacity_aware_assignment_diagnostics(
          bool                                  capacity_aware_enabled
        , bool                                  capacity_aware_search_enabled
        , const CapacityAwareSplitDiagnostics& split_diagnostics
        , Dimless                               used_factor
        , CapacityPenaltyPolicy                 penalty_policy
    );

    [[nodiscard]] CapacityAwareAssignmentDiagnostics make_capacity_aware_assignment_disabled_diagnostics(
        CapacityPenaltyPolicy penalty_policy
    );

    [[nodiscard]] CapacityAwareSplitDiagnostics make_capacity_aware_split_disabled_diagnostics();

}  // namespace timetable::domain::assignment
