#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/cost/types.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_cost_mode(
        SearchCostMode mode
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_capacity_cost_config(
          const SearchCapacityCostConfig& config
        , SearchCostMode                  mode
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_cost_context(
        const SearchCostContext& context
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_cost_components(
        const SearchCostComponents& components
    );

}  // namespace timetable::domain::assignment
