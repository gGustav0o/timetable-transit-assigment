#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/split/impedance_transform.hpp"
#include "timetable/domain/assignment/split/policies.hpp"
#include "timetable/domain/assignment/split/probability.hpp"
#include "timetable/domain/assignment/split/types.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/params/split.hpp"

namespace timetable::domain::assignment {

    struct SplitShareAlternativeView final {
        SplitImpedanceAlternativeView impedance{};
        SplitIndependenceWeight       independence{ 1.0 };
    };

    struct SplitShareAllocationPolicy final {
        SplitImpedancePolicy          impedance{};
        SplitImpedanceTransformPolicy impedance_transform{};
        SplitChoiceModelConfig        choice_model{};
        ProbabilityPolicy             probability{};
    };

    struct SplitShareAllocationResult final {
        std::vector<SplitIndependenceWeight> independences{};
        std::vector<SplitRawImpedance>       split_impedances{};
        SplitAllocation                      allocation;
    };

    [[nodiscard]] mathfp::Expected<SplitShareAllocationResult> compute_split_share_allocation(
          std::span<const SplitShareAlternativeView> alternatives
        , const TimeInterval&                        interval
        , SplitDemandMass                           demand
        , const SplitShareAllocationPolicy&          policy
    );

}  // namespace timetable::domain::assignment
