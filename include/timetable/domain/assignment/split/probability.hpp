#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/split/policies.hpp"
#include "timetable/domain/assignment/split/types.hpp"

namespace timetable::domain::assignment {

    struct SplitAllocation final {
        std::vector<SplitProbabilityMass> probabilities{};
        std::vector<SplitPassengerMass>   passengers{};
        std::size_t                       residual_index{};
        std::size_t                       suppressed_numerical_shares{};
    };

    [[nodiscard]] mathfp::Expected<SplitAllocation> normalize_split_log_weights(
          std::span<const SplitLogWeight> log_weights
        , SplitDemandMass                 demand_passengers
        , const ProbabilityPolicy&         policy = {}
    );

}  // namespace timetable::domain::assignment
