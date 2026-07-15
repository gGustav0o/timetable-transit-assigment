#pragma once

#include <cstdint>
#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    struct SearchTemporalSuitabilityPolicy final {
        TransferLimits transfer_limits{};
    };

    struct SearchNodeRelevancePolicy final {
        ExactPruningPolicy exact{};
    };

    struct SearchNodeTolerancePolicy final {
        SearchTolerances tolerances{};
    };

    enum class SearchLoopTransferPolicy : std::uint8_t {
        SameLineReboardingOnlyForTimeSavingLoop
    };

    struct SearchLoopPolicy final {
        SearchLoopTransferPolicy transfer_policy{
            SearchLoopTransferPolicy::SameLineReboardingOnlyForTimeSavingLoop
        };
    };

    /**
     * @brief Mathematical policy used while building a connection tree.
     *
     * This is the complete branch-and-bound kernel policy. It deliberately
     * excludes final connection choice, demand projection, runtime rollout,
     * logging, cancellation and output concerns.
     */
    struct SearchPolicy final {
        SearchCostContext               cost{};
        SearchTemporalSuitabilityPolicy temporal{};
        SearchNodeRelevancePolicy       relevance{};
        SearchNodeTolerancePolicy       tolerance{};
        SearchLoopPolicy                loops{};
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_temporal_suitability_policy(
        const SearchTemporalSuitabilityPolicy& policy
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_node_relevance_policy(
        const SearchNodeRelevancePolicy& policy
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_node_tolerance_policy(
        const SearchNodeTolerancePolicy& policy
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_loop_policy(
        const SearchLoopPolicy& policy
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_policy(
        const SearchPolicy& policy
    );

    [[nodiscard]] mathfp::Expected<SearchPolicy> make_search_policy(
          SearchCostContext cost
        , const SearchParams& params
    );

}  // namespace timetable::domain::assignment
