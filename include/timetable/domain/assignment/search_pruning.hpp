#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    struct SearchPruningExecutionPlan;

    /**
     * @brief Search-state identity for state-local label retention.
     *
     * Mathematical role:
     * labels are compared for dominance only within the same state key.
     * The correctness of exact dominance therefore depends on whether this key
     * is extension-safe for the current search semantics.
     *
     * Current state space:
     * - physical endpoint
     * - optional stop occurrence
     *
     * This mirrors the current search implementation without yet changing the
     * state factorization.
     */
    struct SearchPruningStateKey final {
        EndpointKey                      physical{};
        std::optional<StopOccurrenceKey> occurrence{};

        auto operator<=>(const SearchPruningStateKey&) const = default;
    };

    /**
     * @brief Primitive coordinates of a partial connection label.
     *
     * These coordinates are treated as primitive with respect to pruning
     * semantics and future extension reasoning:
     * - departure
     * - arrival
     * - transfers
     * - fare
     */
    struct SearchPruningLabelPrimitive final {
        Time          departure{};
        Time          arrival{};
        TransferCount transfers{};
        double        fare{};
    };

    /**
     * @brief Derived coordinates of a partial connection label.
     *
     * These values are deterministic functions of the partial connection under
     * the current metric model and are stored explicitly to keep pruning pure:
     * - journey_time
     * - walk_time
     * - impedance
     */
    struct SearchPruningLabelDerived final {
        Time          journey_time{};
        Time          walk_time{};
        double        impedance{};
    };

    /**
     * @brief Self-contained partial connection label used by pruning.
     */
    struct SearchPruningLabel final {
        SearchPruningLabelPrimitive primitive{};
        SearchPruningLabelDerived   derived{};
    };

    /**
     * @brief Exact-dominance contract for the current pruning state space.
     *
     * ExtensionSafeCurrentState means:
     * if two labels share the same SearchPruningStateKey, exact dominance is
     * assumed safe for the current search semantics and state factorization.
     *
     * This contract is a specification object, not a runtime optimization knob.
     * If the search state space changes in the future, this contract must be
     * revisited before reusing the same exact dominance rule.
     */
    enum class ExactDominanceContract : std::uint8_t {
        ExtensionSafeCurrentState
    };

    /**
     * @brief State-local summary used by approximate retention.
     *
     * It stores minima over a canonical label set at one state key.
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
     * @brief Canonical state-local label container for pruning.
     *
     * Invariant:
     * - labels are exact-nondominated within one state key
     * - labels are ordered by arrival time
     * - summary agrees with labels
     */
    struct SearchPruningLabelSet final {
        std::vector<SearchPruningLabel> labels{};
        SearchPruningSummary            summary{};
    };

    /**
     * @brief Exact retention policy for pruning.
     *
     * This layer is mathematically exact:
     * a candidate is rejected only when already exact-dominated in the same
     * state, or when it exact-dominates existing labels which should then be
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

    enum class SearchPruningReason : std::uint8_t {
          Accepted
        , RejectedExactDominance
        , RejectedApproximateTolerance
    };

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

    mathfp::Expected<SearchPruningLabel> make_search_pruning_label(
          SearchPruningLabelPrimitive primitive
        , SearchPruningLabelDerived   derived
    );

    mathfp::Expected<mathfp::Unit> validate_search_pruning_label(
        const SearchPruningLabel& label
    );

    mathfp::Expected<mathfp::Unit> validate_search_pruning_summary(
        const SearchPruningSummary& summary
    );

    mathfp::Expected<mathfp::Unit> validate_search_pruning_label_set(
        const SearchPruningLabelSet& label_set
    );

    [[nodiscard]] bool dominates_exactly(
          ExactDominanceContract    contract
        , const SearchPruningLabel& lhs
        , const SearchPruningLabel& rhs
    ) noexcept;

    [[nodiscard]] bool dominates_exactly(
          const ExactPruningPolicy& policy
        , const SearchPruningLabel& lhs
        , const SearchPruningLabel& rhs
    ) noexcept;

    [[nodiscard]] bool dominates_exactly(
          const SearchPruningLabel& lhs
        , const SearchPruningLabel& rhs
    ) noexcept;

    [[nodiscard]] bool is_exactly_relevant(
          const ExactPruningPolicy&           policy
        , const SearchPruningLabel&           candidate
        , std::span<const SearchPruningLabel> known
    ) noexcept;

    [[nodiscard]] bool is_exactly_relevant(
          const SearchPruningLabel&           candidate
        , std::span<const SearchPruningLabel> known
    ) noexcept;

    [[nodiscard]] SearchPruningSummary summarize_pruning_labels(
        std::span<const SearchPruningLabel> labels
    ) noexcept;

    [[nodiscard]] bool within_approximate_retention(
          const SearchPruningLabel&       candidate
        , const SearchPruningSummary&     summary
        , const ApproximatePruningPolicy& policy
        , const TransferLimits&           limits
    ) noexcept;

    [[nodiscard]] ExactPruningDecision evaluate_exact_pruning(
          const ExactPruningPolicy&    exact_policy
        , const SearchPruningLabel&    candidate
        , const SearchPruningLabelSet& label_set
    ) noexcept;

    [[nodiscard]] ExactPruningDecision evaluate_exact_pruning(
          const SearchPruningLabel&    candidate
        , const SearchPruningLabelSet& label_set
    ) noexcept;

    [[nodiscard]] ApproximatePruningDecision evaluate_approximate_pruning(
          const ApproximatePruningPolicy& approximate_policy
        , const SearchPruningLabel&       candidate
        , const SearchPruningSummary&     summary
        , const TransferLimits&           limits
    ) noexcept;

    SearchPruningLabelSet insert_exact_pruning_label(
          const ExactPruningPolicy& exact_policy
        , SearchPruningLabelSet     label_set
        , SearchPruningLabel        label
    );

    SearchPruningLabelSet insert_exact_pruning_label(
          SearchPruningLabelSet label_set
        , SearchPruningLabel    label
    );

    /**
     * @brief Evaluate a candidate against exact and approximate pruning layers.
     *
     * Order:
     * 1. exact dominance
     * 2. approximate retention
     *
     * This function is intentionally pure and does not mutate the label set.
     */
    [[nodiscard]] SearchPruningDecision evaluate_search_pruning(
          const ExactPruningPolicy&       exact_policy
        , const SearchPruningLabel&       candidate
        , const SearchPruningLabelSet&    label_set
        , const ApproximatePruningPolicy& approximate_policy
        , const TransferLimits&           limits
    ) noexcept;

    [[nodiscard]] SearchPruningDecision evaluate_search_pruning(
          const SearchPruningLabel&       candidate
        , const SearchPruningLabelSet&    label_set
        , const ApproximatePruningPolicy& approximate_policy
        , const TransferLimits&           limits
    ) noexcept;

    [[nodiscard]] bool stores_search_pruning_labels(
        const SearchPruningExecutionPlan& execution
    ) noexcept;

    [[nodiscard]] SearchPruningDecision evaluate_search_pruning(
          const SearchPruningExecutionPlan& execution
        , const SearchPruningLabel&         candidate
        , const SearchPruningLabelSet&      label_set
        , const TransferLimits&             limits
    ) noexcept;

    SearchPruningLabelSet insert_search_pruning_label(
          const SearchPruningExecutionPlan& execution
        , SearchPruningLabelSet             label_set
        , SearchPruningLabel                label
    );

}  // namespace timetable::domain::assignment
