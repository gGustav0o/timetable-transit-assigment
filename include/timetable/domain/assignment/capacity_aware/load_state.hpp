#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/capacity_aware_assignment.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load_state(
        const VehicleJourneyItemLoadState& state
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoadState> make_vehicle_journey_item_load_state(
        std::vector<VehicleJourneyItemLoad> items
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoadState> make_vehicle_journey_item_load_state(
        const VehicleJourneyItemLoads& loads
    );

}  // namespace timetable::domain::assignment
