#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/split/demand_projection.hpp"
#include "timetable/domain/assignment/split/load_accumulation.hpp"
#include "timetable/domain/assignment/split/policies.hpp"
#include "timetable/domain/assignment/split/probability.hpp"
#include "timetable/domain/assignment/split/types.hpp"

namespace timetable::domain::assignment {

    struct SplitKernelPolicy {
        ProbabilityPolicy probability;
        DemandProjectionPolicy demand_projection;
        LoadAccumulationPolicy load_accumulation;
    };

    struct SplitKernelResult final {
        std::vector<SplitProbabilityMass> probabilities{};
        std::vector<SplitPassengerMass>   passengers{};
        std::vector<double> segment_loads{};
        std::vector<double> stop_loads{};
        std::vector<double> trip_loads{};
        std::size_t total_alternatives{};
        std::size_t significant_alternatives{};
        std::size_t suppressed_alternatives{};
        double total_demand{};
        double total_load{};
    };

    [[nodiscard]] mathfp::Expected<SplitKernelResult> execute_split_kernel(
          std::span<const SplitLogWeight> log_weights
        , SplitDemandMass                  demand
        , std::span<const std::size_t>    trip_indices
        , std::span<const std::size_t>    stop_indices
        , std::span<const std::size_t>    segment_indices
        , const SplitKernelPolicy&         policy
    );

}  // namespace timetable::domain::assignment
