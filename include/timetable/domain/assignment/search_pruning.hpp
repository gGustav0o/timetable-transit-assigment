#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <array>
#include <string_view>
#include <vector>

#include <boost/container/small_vector.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    struct SearchPruningExecutionPlan;

    /**
     * @brief Search-state identity for state-local metric retention.
     *
     * Mathematical role:
     * partial metric vectors are compared for dominance only within the same state key.
     * The correctness of exact dominance therefore depends on whether this key
     * is extension-safe for the current search semantics.
     *
     * Current state space:
     * - physical endpoint
     * - optional stop occurrence
     * - structural branch phase
     * - transfer context of the last timed leg
     *
     * The phase is required because walk-phase states can share the same
     * physical endpoint while having different admissible continuations.
     * The transfer context is required because feasibility of future timed
     * successors depends not only on the current physical/occurrence position,
     * but also on the last timed trip/line through same-trip and same-line
     * transfer restrictions.
     */
    struct SearchPruningTransferContext final {
        std::optional<TripId> last_trip{};
        std::optional<LineId> last_line{};

        auto operator<=>(const SearchPruningTransferContext&) const = default;
    };

    struct SearchPruningStateKey final {
        EndpointKey                       physical{};
        std::optional<StopOccurrenceKey>  occurrence{};
        SearchBranchPhase                 phase{ SearchBranchPhase::AtOrigin };
        SearchPruningTransferContext      transfer{};

        auto operator<=>(const SearchPruningStateKey&) const = default;
    };

    /**
     * @brief Metric vector of a partial connection prefix used by pruning.
     *
     * The first six fields are parameter-independent partial metrics. The last
     * field is the current search-model evaluation of these metrics. Dominance
     * and approximate retention are functions of this vector, never of a
     * connection entity.
     */
    struct SearchPruningMetrics final {
        Time          departure{};
        Time          arrival{};
        Time          journey_time{};
        Time          walk_time{};
        TransferCount transfers{};
        double        fare{};
        double        impedance{};
    };

    using SearchPruningMetricVector = boost::container::small_vector<
          SearchPruningMetrics
        , 1
    >;

    /**
     * @brief Exact-dominance contract for the current pruning state space.
     *
     * ExtensionSafeCurrentState means:
     * if two metric vectors share the same SearchPruningStateKey, exact dominance is
     * assumed safe for the current search semantics and state factorization.
     *
     * This contract is a specification object, not a runtime optimization knob.
     * If the search state space changes in the future, this contract must be
     * revisited before reusing the same exact dominance rule.
     */
    enum class ExactDominanceContract : std::uint8_t {
        ExtensionSafeCurrentState
    };

    inline constexpr std::array kExactDominanceContractTokens{
        timetable::EnumStringEntry<ExactDominanceContract>{
            ExactDominanceContract::ExtensionSafeCurrentState,
            "extension_safe_current_state"
        }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        ExactDominanceContract value
    ) noexcept {
        return timetable::enum_to_string(value, kExactDominanceContractTokens);
    }

    [[nodiscard]] inline constexpr std::optional<ExactDominanceContract> exact_dominance_contract_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kExactDominanceContractTokens);
    }

    /**
     * @brief State-local summary used by approximate retention.
     *
     * It stores minima over a canonical metric set at one state key.
     * The owning metric set is additionally task-local: these minima must not
     * be reused across different OD-interval SearchTask instances.
     */
    struct SearchPruningSummary final {
        double min_impedance    { 0.0 };
        double min_journey_time { 0.0 };
        double min_walk_time    { 0.0 };
        double min_transfers    { 0.0 };
        double min_fare         { 0.0 };
        bool   empty            { true };
    };

    /**
     * @brief Canonical state-local metric container for pruning.
     *
     * Invariant:
     * - the container belongs to one SearchTask and one state key
     * - metrics are exact-nondominated within one state key
     * - metrics are ordered by arrival time
     * - summary agrees with metrics
     */
    struct SearchPruningMetricSet final {
        SearchPruningMetricVector metrics{};
        SearchPruningSummary              summary{};
    };

    /**
     * @brief Exact retention policy for pruning.
     *
     * This layer is mathematically exact:
     * a candidate is rejected only when already exact-dominated in the same
     * state, or when it exact-dominates existing metric vectors which should then be
     * removed from the canonical set.
     *
     * The contract explicitly states under which state factorization the
     * dominance relation is assumed extension-safe.
     */
    struct ExactPruningPolicy final {
        ExactDominanceContract contract{
            ExactDominanceContract::ExtensionSafeCurrentState
        };
    };

    /**
     * @brief Approximate retention policy for pruning.
     *
     * This layer is intentionally non-exact:
     * it applies tolerance constraints relative to the state-local minima.
     * Therefore it must remain conceptually separate from exact dominance.
     */
    struct ApproximatePruningPolicy final {
        SearchTolerances tolerances{};
    };

    enum class SearchPruningLayer : std::uint8_t {
          Exact
        , Approximate
    };

    inline constexpr std::array kSearchPruningLayerTokens{
          timetable::EnumStringEntry<SearchPruningLayer>{
              SearchPruningLayer::Exact, "exact"
          }
        , timetable::EnumStringEntry<SearchPruningLayer>{
              SearchPruningLayer::Approximate, "approximate"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchPruningLayer value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchPruningLayerTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchPruningLayer> search_pruning_layer_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchPruningLayerTokens);
    }

    enum class SearchPruningReason : std::uint8_t {
          Accepted
        , RejectedExactDominance
        , RejectedApproximateTolerance
    };

    inline constexpr std::array kSearchPruningReasonTokens{
          timetable::EnumStringEntry<SearchPruningReason>{
              SearchPruningReason::Accepted, "accepted"
          }
        , timetable::EnumStringEntry<SearchPruningReason>{
              SearchPruningReason::RejectedExactDominance,
              "rejected_exact_dominance"
          }
        , timetable::EnumStringEntry<SearchPruningReason>{
              SearchPruningReason::RejectedApproximateTolerance,
              "rejected_approximate_tolerance"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchPruningReason value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchPruningReasonTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchPruningReason> search_pruning_reason_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchPruningReasonTokens);
    }

    struct ExactPruningDecision final {
        SearchPruningReason reason   { SearchPruningReason::Accepted };
        bool                accepted { true };
    };

    struct ApproximatePruningDecision final {
        SearchPruningReason reason   { SearchPruningReason::Accepted };
        bool                accepted { true };
    };

    struct SearchPruningDecision final {
        SearchPruningLayer  layer    { SearchPruningLayer::Exact };
        SearchPruningReason reason   { SearchPruningReason::Accepted };
        bool                accepted { true };
    };

    mathfp::Expected<SearchPruningMetrics> make_search_pruning_metrics(
        SearchPruningMetrics metrics
    );

    mathfp::Expected<mathfp::Unit> validate_search_pruning_metrics(
        const SearchPruningMetrics& metrics
    );

    mathfp::Expected<mathfp::Unit> validate_search_pruning_summary(
        const SearchPruningSummary& summary
    );

    mathfp::Expected<mathfp::Unit> validate_search_pruning_metric_set(
        const SearchPruningMetricSet& metric_set
    );

    [[nodiscard]] bool dominates_exactly(
          ExactDominanceContract    contract
        , const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept;

    [[nodiscard]] bool dominates_exactly(
          const ExactPruningPolicy& policy
        , const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept;

    [[nodiscard]] bool dominates_exactly(
          const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept;

    [[nodiscard]] bool is_exactly_relevant(
          const ExactPruningPolicy&           policy
        , const SearchPruningMetrics&         candidate
        , std::span<const SearchPruningMetrics> known
    ) noexcept;

    [[nodiscard]] bool is_exactly_relevant(
          const SearchPruningMetrics&         candidate
        , std::span<const SearchPruningMetrics> known
    ) noexcept;

    [[nodiscard]] SearchPruningSummary summarize_pruning_metrics(
        std::span<const SearchPruningMetrics> metrics
    ) noexcept;

    [[nodiscard]] bool within_approximate_retention(
          const SearchPruningMetrics&     candidate
        , const SearchPruningSummary&     summary
        , const ApproximatePruningPolicy& policy
        , const TransferLimits&           limits
    ) noexcept;

    [[nodiscard]] ExactPruningDecision evaluate_exact_pruning(
          const ExactPruningPolicy&    exact_policy
        , const SearchPruningMetrics&  candidate
        , const SearchPruningMetricSet& metric_set
    ) noexcept;

    [[nodiscard]] ExactPruningDecision evaluate_exact_pruning(
          const SearchPruningMetrics&  candidate
        , const SearchPruningMetricSet& metric_set
    ) noexcept;

    [[nodiscard]] ApproximatePruningDecision evaluate_approximate_pruning(
          const ApproximatePruningPolicy& approximate_policy
        , const SearchPruningMetrics&     candidate
        , const SearchPruningSummary&     summary
        , const TransferLimits&           limits
    ) noexcept;

    SearchPruningMetricSet insert_exact_pruning_metrics(
          const ExactPruningPolicy& exact_policy
        , SearchPruningMetricSet    metric_set
        , SearchPruningMetrics      metrics
    );

    void insert_exact_pruning_metrics_in_place(
          const ExactPruningPolicy& exact_policy
        , SearchPruningMetricSet&   metric_set
        , SearchPruningMetrics      metrics
    );

    SearchPruningMetricSet insert_exact_pruning_metrics(
          SearchPruningMetricSet metric_set
        , SearchPruningMetrics   metrics
    );

    /**
     * @brief Evaluate a candidate against exact and approximate pruning layers.
     *
     * Order:
     * 1. exact dominance
     * 2. approximate retention
     *
     * This function is intentionally pure and does not mutate the metric set.
     */
    [[nodiscard]] SearchPruningDecision evaluate_search_pruning(
          const ExactPruningPolicy&       exact_policy
        , const SearchPruningMetrics&     candidate
        , const SearchPruningMetricSet&   metric_set
        , const ApproximatePruningPolicy& approximate_policy
        , const TransferLimits&           limits
    ) noexcept;

    [[nodiscard]] SearchPruningDecision evaluate_search_pruning(
          const SearchPruningMetrics&     candidate
        , const SearchPruningMetricSet&   metric_set
        , const ApproximatePruningPolicy& approximate_policy
        , const TransferLimits&           limits
    ) noexcept;

    [[nodiscard]] bool stores_search_pruning_metrics(
        const SearchPruningExecutionPlan& execution
    ) noexcept;

    [[nodiscard]] SearchPruningDecision evaluate_search_pruning(
          const SearchPruningExecutionPlan& execution
        , const SearchPruningMetrics&       candidate
        , const SearchPruningMetricSet&     metric_set
        , const TransferLimits&             limits
    ) noexcept;

    SearchPruningMetricSet insert_search_pruning_metrics(
          const SearchPruningExecutionPlan& execution
        , SearchPruningMetricSet            metric_set
        , SearchPruningMetrics              metrics
    );

    void insert_search_pruning_metrics_in_place(
          const SearchPruningExecutionPlan& execution
        , SearchPruningMetricSet&           metric_set
        , SearchPruningMetrics              metrics
    );

}  // namespace timetable::domain::assignment
