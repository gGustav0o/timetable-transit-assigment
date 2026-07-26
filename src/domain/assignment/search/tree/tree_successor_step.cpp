#include "timetable/domain/assignment/search/tree/tree_successor_step.hpp"

#include <utility>

#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<TreeSuccessorStepDecision>
    evaluate_tree_successor_step(
          const BranchArena&              branches
        , std::size_t                     branch_index
        , const SearchBranch&             branch
        , const PreprocessedNetwork&      network
        , const SearchSuccessor&          successor
        , std::optional<IntervalId>       interval
        , const SearchTimeDomain*         first_departure_domain
        , const TransferLimits&           limits
        , const SearchCostContext&        search_cost
        , TreeSuccessorStepConfig        config
        , RetainedConnectionLabelRegistry&   label_registry
        , NodeConnectionSetMap&   node_connection_sets
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&      pruning_stats
    ) {
        if (!config.late_feasibility_prechecked) {
            const auto feasibility_decision =
                evaluate_search_successor_feasibility(
                      branch
                    , network
                    , successor
                    , first_departure_domain
                    , limits
                );
            if (!feasibility_decision.accepted()) {
                return TreeSuccessorStepDecision{
                      .rejection = TreeSuccessorStepRejection::LateFeasibility
                    , .feasibility_rejection = feasibility_decision.rejection
                };
            }
        }

        std::optional<RetainedConnectionLabelId> accepted_retained_label;
        std::optional<NodeConnectionRetentionDecision> retention_decision;
        if (config.retain_in_node_connection_sets) {
            MATHFP_TRY_LET(
                  ConnectionPrefixEvaluation
                , connection_prefix
                , evaluate_connection_prefix_before_branch(
                      branches
                    , branch
                    , network
                    , successor
                    , interval
                    , search_cost
                )
            );
            if (!connection_prefix.connection_candidate.has_value()) {
                if (connection_prefix.rejection == BranchTransitionRejection::RepeatedPhysicalNode
                    || connection_prefix.rejection == BranchTransitionRejection::RepeatedStopOccurrence) {
                    return TreeSuccessorStepDecision{
                          .rejection = TreeSuccessorStepRejection::PrefixCycle
                        , .prefix_rejection = connection_prefix.rejection
                    };
                }
                if (!connection_prefix.accepted()) {
                    return TreeSuccessorStepDecision{
                          .rejection = TreeSuccessorStepRejection::PrefixRejected
                        , .prefix_rejection = connection_prefix.rejection
                    };
                }
            } else {
                if (connection_prefix.connection_candidate->metrics.transfers
                    > limits.max_transfers) {
                    return TreeSuccessorStepDecision{
                        .rejection = TreeSuccessorStepRejection::PrefixTransferLimit
                    };
                }

                MATHFP_TRY_ASSIGN(
                    retention_decision,
                    retain_connection_tree_node(
                          connection_prefix.connection_candidate->node
                        , std::move(connection_prefix.connection_candidate->metrics)
                        , branch.retained_connection_label
                        , label_registry
                        , node_connection_sets
                        , limits
                        , pruning_execution
                        , pruning_stats
                    )
                );
                if (!retention_decision->accepted()) {
                    return TreeSuccessorStepDecision{
                          .rejection = TreeSuccessorStepRejection::PrefixRetention
                        , .retention = std::move(retention_decision)
                    };
                }
                accepted_retained_label = retention_decision->label;
            }
        }

        MATHFP_TRY_LET(
              BranchTransitionResult
            , transition
            , transition_search_branch_with_diagnostics(
                  branches
                , branch_index
                , branch
                , network
                , successor
                , interval
                , search_cost
            )
        );
        if (!transition.branch.has_value()) {
            return TreeSuccessorStepDecision{
                  .rejection = TreeSuccessorStepRejection::BranchCycle
                , .branch_rejection = transition.diagnostics.rejection
                , .retention = std::move(retention_decision)
            };
        }
        if (transition.branch->metrics.transfers > limits.max_transfers) {
            return TreeSuccessorStepDecision{
                  .accepted_retained_label = accepted_retained_label
                , .rejection = TreeSuccessorStepRejection::BranchTransferLimit
                , .retention = std::move(retention_decision)
            };
        }

        return TreeSuccessorStepDecision{
              .branch = std::move(transition.branch)
            , .accepted_retained_label = accepted_retained_label
            , .retention = std::move(retention_decision)
        };
    }

}  // namespace timetable::domain::assignment
