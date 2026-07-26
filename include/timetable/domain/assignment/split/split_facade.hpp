#pragma once

#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/split/split_kernel.hpp"
#include "timetable/domain/assignment/split/types.hpp"
#include "timetable/domain/params/split.hpp"

namespace timetable::domain::assignment {

    /**
     * Thin facade for demand splitting.
     * Validates input data and delegates to the pure kernel.
     */
    struct SplitFacadePolicy {
        SplitKernelPolicy kernel;
        SplitParams split_params; // Backward-compatible parameter bridge.
    };

    /**
     * Result produced by the demand split facade.
     */
    struct SplitFacadeResult final {
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
        bool validation_success{};
    };

    /**
     * Thin facade for demand splitting.
     *
     * @param log_weights vector of alternative log weights
     * @param demand passenger demand
     * @param trip_indices trip indices for each alternative
     * @param stop_indices stop indices for each alternative
     * @param segment_indices segment indices for each alternative
     * @param policy facade configuration
     */
    [[nodiscard]] mathfp::Expected<SplitFacadeResult> split_demand(
          std::span<const SplitLogWeight> log_weights
        , SplitDemandMass                  demand
        , std::span<const std::size_t>    trip_indices
        , std::span<const std::size_t>    stop_indices
        , std::span<const std::size_t>    segment_indices
        , const SplitFacadePolicy&         policy
    );

}  // namespace timetable::domain::assignment
