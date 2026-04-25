#pragma once

#include <optional>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search_pruning_config.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Executable pruning plan for the current search implementation.
     *
     * This is the runtime bridge between the requested pruning state-space and
     * the staged retention semantics currently enabled in the engine.
     */
    struct SearchPruningExecutionPlan final {
        SearchPruningStateSpace  state_space{
            SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
        };
        SearchPruningRolloutStage rollout_stage{
            SearchPruningRolloutStage::ExactAndApproximateCurrentState
        };
        bool                                    exact_enabled       { true };
        bool                                    approximate_enabled { true };
        ExactPruningPolicy                      exact_policy        {};
        std::optional<ApproximatePruningPolicy> approximate_policy  {};
    };

    mathfp::Expected<SearchPruningExecutionPlan> plan_search_pruning_execution(
          SearchPruningStateSpace   requested_state_space
        , SearchPruningRolloutStage rollout_stage
        , const SearchTolerances&   tolerances
    );

}  // namespace timetable::domain::assignment
