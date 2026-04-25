#pragma once

#include <cstdint>

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
