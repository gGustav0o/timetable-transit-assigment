#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "timetable/enum_string.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Rollout levels for final OD-local choice filtering.
     *
     * ExactOnly:
     * - keep only exact nondominated connections.
     *
     * ExactAndApproximate:
     * - apply exact nondominance first, then the stricter whole-connection
     *   tolerance filtering from the connection-choice stage of the paper.
     */
    enum class ChoiceRolloutStage : std::uint8_t {
          ExactOnly
        , ExactAndApproximate
    };

    inline constexpr std::array kChoiceRolloutStageTokens{
          timetable::EnumStringEntry<ChoiceRolloutStage>{
              ChoiceRolloutStage::ExactOnly, "exact_only"
          }
        , timetable::EnumStringEntry<ChoiceRolloutStage>{
              ChoiceRolloutStage::ExactAndApproximate, "exact_and_approximate"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        ChoiceRolloutStage value
    ) noexcept {
        return timetable::enum_to_string(value, kChoiceRolloutStageTokens);
    }

    [[nodiscard]] inline constexpr std::optional<ChoiceRolloutStage> choice_rollout_stage_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kChoiceRolloutStageTokens);
    }

    /**
     * @brief Domain configuration for final connection choice filtering.
     *
     * This remains separate from SearchParams so rollout policy is not confused
     * with the mathematical connection metrics themselves.
     */
    struct ChoiceConfig final {
        ChoiceRolloutStage rollout_stage{ ChoiceRolloutStage::ExactOnly };
    };

}  // namespace timetable::domain::assignment
