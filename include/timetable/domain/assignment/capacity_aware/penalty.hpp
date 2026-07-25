#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/capacity_aware_assignment.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_penalty_policy(
        CapacityPenaltyPolicy policy
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_capacity_exposure(
        CapacityExposure exposure
    );

    [[nodiscard]] mathfp::Expected<Dimless> capacity_ratio(
          double                            load
        , const VehicleJourneyItemCapacity& capacity
    );

    [[nodiscard]] mathfp::Expected<Dimless> capacity_ratio(
          const VehicleJourneyItemLoad&     load
        , const VehicleJourneyItemCapacity& capacity
    );

    [[nodiscard]] mathfp::Expected<Dimless> capacity_penalty(
          CapacityPenaltyPolicy policy
        , Dimless               ratio
    );

    [[nodiscard]] mathfp::Expected<double> capacity_adjusted_perceived_journey_time(
          double           base_perceived_journey_time
        , CapacityExposure exposure
        , Dimless          factor
    );

    [[nodiscard]] mathfp::Expected<double> capacity_adjusted_split_impedance(
          double           base_split_impedance
        , CapacityExposure exposure
        , Dimless          q_time
        , Dimless          perceived_journey_time_capacity_factor
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> make_capacity_exposure(
        Time equivalent_time
    );

}  // namespace timetable::domain::assignment
