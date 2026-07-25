#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/capacity_aware/penalty.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<CapacityExposure> connection_capacity_exposure(
          const Connection&                  connection
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> connection_capacity_exposure(
          const SearchConnection&            connection
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> day_path_support_capacity_exposure(
          const DayPathSupportDescriptor&    support
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    );

}  // namespace timetable::domain::assignment
