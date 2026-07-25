#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/cost/types.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_connection_impedance_components(
        const ConnectionImpedanceComponents& components
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_impedance_weights(
        const SearchImpedance& impedance
    );

    [[nodiscard]] mathfp::Expected<double> base_search_impedance(
          const ConnectionImpedanceComponents& components
        , const SearchImpedance&               impedance
        , double                               fare_scale
    );

    [[nodiscard]] mathfp::Expected<double> capacity_adjusted_search_impedance(
          double           base_impedance
        , CapacityExposure exposure
        , Dimless          volume_capacity_ratio
    );

    /**
     * @brief Evaluate search impedance for already validated cost data.
     *
     * Full SearchCostContext validation belongs to make_*_search_cost_context
     * and pipeline boundaries. This function is intentionally lightweight
     * because it is called from branch-and-bound dominance and retention loops.
     */
    [[nodiscard]] mathfp::Expected<double> search_impedance(
          const SearchCostComponents& components
        , const SearchCostContext&    context
    );

}  // namespace timetable::domain::assignment
