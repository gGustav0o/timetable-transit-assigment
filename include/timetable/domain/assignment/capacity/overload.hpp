#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/capacity.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemOverloadStatus>
    vehicle_journey_item_overload_status(
          double passengers
        , double total_capacity
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemOverload>
    make_missing_capacity_vehicle_journey_item_overload(
        const VehicleJourneyItemLoad& load
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemOverload>
    compute_vehicle_journey_item_overload(
          const VehicleJourneyItemLoad&     load
        , const VehicleJourneyItemCapacity& capacity
    );

}  // namespace timetable::domain::assignment
