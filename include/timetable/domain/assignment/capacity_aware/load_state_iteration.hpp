#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/capacity_aware_assignment.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] CapacityLoadStateDelta load_state_delta(
          const VehicleJourneyItemLoadState& previous
        , const VehicleJourneyItemLoadState& next
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoadState> msa_update_load_state(
          const VehicleJourneyItemLoadState& previous
        , const VehicleJourneyItemLoads&     candidate
        , double                             alpha
    );

    [[nodiscard]] bool capacity_iteration_converged(
          const CapacityIterationConfig& iteration
        , CapacityLoadStateDelta         delta
    ) noexcept;

}  // namespace timetable::domain::assignment
