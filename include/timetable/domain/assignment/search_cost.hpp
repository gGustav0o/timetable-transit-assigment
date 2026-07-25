#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/cost/types.hpp"
#include "timetable/domain/assignment/search/cost/capacity_index.hpp"
#include "timetable/domain/assignment/search/cost/exposure.hpp"
#include "timetable/domain/assignment/search/cost/impedance.hpp"
#include "timetable/domain/assignment/search/cost/validation.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<SearchCapacityCostConfig> make_base_search_capacity_cost_config();

    [[nodiscard]] mathfp::Expected<SearchCostContext> make_base_search_cost_context(
          SearchImpedance impedance
        , double          fare_scale
    );

    [[nodiscard]] mathfp::Expected<SearchCostContext> make_capacity_aware_search_cost_context(
          SearchImpedance               impedance
        , double                        fare_scale
        , CapacityPenaltyPolicy         penalty_policy
        , VehicleJourneyItemLoadState   load_state
        , VehicleJourneyItemCapacitySet capacity_set
    );

    [[nodiscard]] mathfp::Expected<SearchCostComponents> make_search_cost_components(
          ConnectionImpedanceComponents base
        , CapacityExposure              capacity_exposure
    );

}  // namespace timetable::domain::assignment
