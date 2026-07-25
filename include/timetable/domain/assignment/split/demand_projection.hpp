#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/split/policies.hpp"
#include "timetable/domain/assignment/split/types.hpp"

namespace timetable::domain::assignment {

    struct DemandProjectionResult final {
        std::vector<SplitProbabilityMass> probabilities{};
        std::vector<SplitPassengerMass>   passengers{};
        std::size_t                       residual_index{};
        std::size_t                       significant_alternatives{};
        std::size_t                       suppressed_alternatives{};
    };

    [[nodiscard]] mathfp::Expected<DemandProjectionResult> project_demand(
          std::span<const SplitProbabilityMass> probabilities
        , SplitDemandMass                         demand
        , std::size_t                             residual_index
        , const DemandProjectionPolicy&           policy = {}
    );

}  // namespace timetable::domain::assignment
