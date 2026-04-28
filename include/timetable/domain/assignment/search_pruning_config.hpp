#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "timetable/enum_string.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Search-state factorization requested by the pruning model.
     *
     * CurrentPhysicalOccurrenceAndTransferContext preserves the current search
     * semantics:
     * pruning compares partial metric vectors only within the same physical
     * endpoint, the same optional stop occurrence, and the same
     * continuation-relevant transfer context carried by the previous timed leg.
     */
    enum class SearchPruningStateSpace : std::uint8_t {
        CurrentPhysicalOccurrenceAndTransferContext
    };

    inline constexpr std::array kSearchPruningStateSpaceTokens{
        timetable::EnumStringEntry<SearchPruningStateSpace>{
            SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext,
            "current_physical_occurrence_and_transfer_context"
        }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchPruningStateSpace value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchPruningStateSpaceTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchPruningStateSpace> search_pruning_state_space_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchPruningStateSpaceTokens);
    }

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

    inline constexpr std::array kSearchPruningRolloutStageTokens{
          timetable::EnumStringEntry<SearchPruningRolloutStage>{
              SearchPruningRolloutStage::Disabled, "disabled"
          }
        , timetable::EnumStringEntry<SearchPruningRolloutStage>{
              SearchPruningRolloutStage::ExactCurrentState, "exact_current_state"
          }
        , timetable::EnumStringEntry<SearchPruningRolloutStage>{
              SearchPruningRolloutStage::ExactAndApproximateCurrentState,
              "exact_and_approximate_current_state"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchPruningRolloutStage value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchPruningRolloutStageTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SearchPruningRolloutStage> search_pruning_rollout_stage_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSearchPruningRolloutStageTokens);
    }

    /**
     * @brief Mathematical request for search pruning.
     *
     * This belongs to the domain model and says what pruning state factorization
     * is requested, independently of the current rollout stage.
     */
    struct SearchPruningModelConfig final {
        SearchPruningStateSpace requested_state_space{
            SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
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
