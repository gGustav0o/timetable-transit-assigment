#pragma once

#include <cstdint>

#include "timetable/domain/assignment/search_pruning.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Search-state factorization requested by the pruning model.
     *
     * CurrentPhysicalAndOccurrence preserves the current search semantics:
     * pruning compares labels only within the same physical endpoint and,
     * for line states, the same stop occurrence.
     */
    enum class SearchPruningStateSpace : std::uint8_t {
        CurrentPhysicalAndOccurrence
    };

    /**
     * @brief Incremental rollout levels for pruning retention.
     *
     * Disabled:
     * - retention is not applied after feasibility.
     *
     * ExactCurrentState:
     * - only exact dominance is applied on the current state space.
     *
     * ExactAndApproximateCurrentState:
     * - exact dominance plus approximate tolerance retention on the current
     *   state space.
     */
    enum class SearchPruningRolloutStage : std::uint8_t {
          Disabled
        , ExactCurrentState
        , ExactAndApproximateCurrentState
    };

    /**
     * @brief Mathematical request for search pruning.
     *
     * This belongs to the domain model and says what pruning state factorization
     * is requested, independently of the current rollout stage.
     */
    struct SearchPruningModelConfig final {
        SearchPruningStateSpace requested_state_space{
            SearchPruningStateSpace::CurrentPhysicalAndOccurrence
        };
    };

    /**
     * @brief Internal rollout policy for the currently implemented search engine.
     */
    struct SearchPruningRuntimeConfig final {
        SearchPruningRolloutStage rollout_stage{
            SearchPruningRolloutStage::ExactAndApproximateCurrentState
        };
    };

    /**
     * @brief Full domain configuration for pruning retention.
     *
     * Parsing remains an outer concern. Data sources and future params parsers
     * populate this type; the type itself stays in the domain layer.
     */
    struct SearchPruningConfig final {
        SearchPruningModelConfig   model{};
        SearchPruningRuntimeConfig runtime{};
    };

}  // namespace timetable::domain::assignment
