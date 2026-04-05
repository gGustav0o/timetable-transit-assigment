#include "timetable/domain/assignment/search_pruning_plan.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<SearchPruningExecutionPlan> plan_search_pruning_execution(
          SearchPruningStateSpace   requested_state_space
        , SearchPruningRolloutStage rollout_stage
        , const SearchTolerances&   tolerances
    ) {
        SearchPruningExecutionPlan plan{
              .state_space         = requested_state_space
            , .rollout_stage       = rollout_stage
            , .exact_enabled       = false
            , .approximate_enabled = false
            , .exact_policy        = ExactPruningPolicy{}
            , .approximate_policy  = std::nullopt
        };

        switch (rollout_stage) {
            case SearchPruningRolloutStage::Disabled:
                return plan;

            case SearchPruningRolloutStage::ExactCurrentState:
                plan.exact_enabled = true;
                return plan;

            case SearchPruningRolloutStage::ExactAndApproximateCurrentState:
                plan.exact_enabled       = true;
                plan.approximate_enabled = true;
                plan.approximate_policy  = ApproximatePruningPolicy{
                    .tolerances = tolerances
                };
                return plan;
        }

        return plan;
    }

}  // namespace timetable::domain::assignment
