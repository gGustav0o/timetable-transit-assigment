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
     * semantics. The historical token name is kept for configuration
     * compatibility; the concrete key also contains the branch phase because
     * walk-phase states have different admissible continuations:
     * pruning compares partial metric vectors only within the same physical
     * endpoint, the same optional stop occurrence, the same branch phase, and the same
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
     * @brief Stop reference used when forming an equivalent-connection key.
     *
     * The policy is intentionally part of the mathematical pruning model, not
     * of complete-connection retention or choice. Equivalent-connection
     * dominance is a partial-search dominance rule: it may compare only branch
     * prefixes whose future feasible continuations are represented by the same
     * extension-safe search state.
     *
     * searchPara.useLastStopForEquivalentConnections selects the stop occurrence
     * component of this key:
     * - false: CurrentStopOccurrence, the current branch occurrence;
     * - true: LastTimedStopOccurrence, the arrival occurrence of the last timed
     *   ride, with a conservative fallback to the current occurrence before the
     *   first timed ride.
     */
    enum class EquivalentConnectionStopReference : std::uint8_t {
          CurrentStopOccurrence
        , LastTimedStopOccurrence
    };

    inline constexpr std::array kEquivalentConnectionStopReferenceTokens{
          timetable::EnumStringEntry<EquivalentConnectionStopReference>{
              EquivalentConnectionStopReference::CurrentStopOccurrence,
              "current_stop_occurrence"
          }
        , timetable::EnumStringEntry<EquivalentConnectionStopReference>{
              EquivalentConnectionStopReference::LastTimedStopOccurrence,
              "last_timed_stop_occurrence"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        EquivalentConnectionStopReference value
    ) noexcept {
        return timetable::enum_to_string(value, kEquivalentConnectionStopReferenceTokens);
    }

    [[nodiscard]] inline constexpr std::optional<EquivalentConnectionStopReference>
    equivalent_connection_stop_reference_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kEquivalentConnectionStopReferenceTokens);
    }

    /**
     * @brief Domain configuration for partial-search equivalent dominance.
     *
     * searchPara.allowDominanceForEquivalentConnections controls only the
     * partial branch-retention dominance relation. It must not be interpreted
     * as complete-connection dominance and must not affect choice retention.
     */
    struct EquivalentConnectionDominanceConfig final {
        bool allow_dominance_for_equivalent_connections{ true };
        EquivalentConnectionStopReference stop_reference{
            EquivalentConnectionStopReference::CurrentStopOccurrence
        };
    };

    /**
     * @brief Typed projection of a partial search branch used for pruning keys.
     *
     * The search engine owns branch storage and trace construction. The domain
     * pruning layer needs only continuation-relevant state components, exposed
     * here as values rather than engine-specific branch objects.
     */
    struct SearchPruningStateProjection final {
        EndpointKey                      physical{};
        std::optional<StopOccurrenceKey> current_occurrence{};
        std::optional<StopOccurrenceKey> last_timed_occurrence{};
        SearchBranchPhase                phase{ SearchBranchPhase::AtOrigin };
        SearchPruningTransferContext     transfer{};
    };

    [[nodiscard]] inline std::optional<StopOccurrenceKey>
    equivalent_connection_stop_occurrence(
          const SearchPruningStateProjection& projection
        , EquivalentConnectionStopReference stop_reference
    ) noexcept {
        switch (stop_reference) {
            case EquivalentConnectionStopReference::CurrentStopOccurrence:
                return projection.current_occurrence;

            case EquivalentConnectionStopReference::LastTimedStopOccurrence:
                return projection.last_timed_occurrence.has_value()
                    ? projection.last_timed_occurrence
                    : projection.current_occurrence;
        }

        return projection.current_occurrence;
    }

    [[nodiscard]] inline SearchPruningStateKey make_search_pruning_state_key(
          SearchPruningStateProjection projection
        , EquivalentConnectionStopReference stop_reference
    ) noexcept {
        return SearchPruningStateKey{
              .physical   = projection.physical
            , .occurrence = equivalent_connection_stop_occurrence(
                  projection
                , stop_reference
              )
            , .phase      = projection.phase
            , .transfer   = projection.transfer
        };
    }

    [[nodiscard]] inline SearchPruningStateKey make_search_pruning_state_key(
          SearchPruningStateProjection projection
        , const EquivalentConnectionDominanceConfig& equivalent
    ) noexcept {
        return make_search_pruning_state_key(
              projection
            , equivalent.stop_reference
        );
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
        EquivalentConnectionDominanceConfig equivalent_connection_dominance{};
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
