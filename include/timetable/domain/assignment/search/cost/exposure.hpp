#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/cost/types.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const Connection&               connection
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const ConnectionLeg&            ride_leg
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const SearchConnection&         connection
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    );

}  // namespace timetable::domain::assignment
