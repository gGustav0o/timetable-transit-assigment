#include "timetable/domain/assignment/search_pruning_plan.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<SearchPruningExecutionPlan> plan_search_pruning_execution(
          SearchPruningModelConfig  model
        , SearchPruningRolloutStage rollout_stage
        , const SearchTolerances&   tolerances
    ) {
        SearchPruningExecutionPlan plan{
              .state_space                        = model.requested_state_space
            , .rollout_stage                      = rollout_stage
            , .equivalent_connection_dominance    = model.equivalent_connection_dominance
            , .exact_enabled                      = false
            , .approximate_enabled                = false
            , .exact_policy                       = ExactPruningPolicy{}
            , .approximate_policy                 = std::nullopt
        };

        if (!model.equivalent_connection_dominance.allow_dominance_for_equivalent_connections) {
            return plan;
        }

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
