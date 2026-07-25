#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/cost/types.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] bool empty_capacity_cost_support(
        const SearchCapacityCostConfig& config
    ) noexcept;

    [[nodiscard]] mathfp::Expected<std::vector<IntervalId>> search_capacity_load_intervals(
        const VehicleJourneyItemLoadState& load_state
    );

    [[nodiscard]] mathfp::Expected<SearchCapacityCostIndex> build_search_capacity_cost_index(
          const VehicleJourneyItemLoadState&   load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy                penalty_policy
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_capacity_cost_index(
        const SearchCapacityCostConfig& config
    );

    [[nodiscard]] mathfp::Expected<double> search_capacity_penalty_sum(
          const SearchCapacityCostConfig& config
        , IntervalId                      interval
        , TripId                          trip
        , RoutePosition                   first
        , RoutePosition                   last
    );

}  // namespace timetable::domain::assignment
