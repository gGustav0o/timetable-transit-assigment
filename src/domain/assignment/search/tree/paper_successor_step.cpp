#include "timetable/domain/assignment/search/tree/paper_successor_step.hpp"

#include <utility>

#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<PaperSuccessorStepDecision>
    evaluate_paper_successor_step(
          const BranchArena&              branches
        , std::size_t                     branch_index
        , const SearchBranch&             branch
        , const PreprocessedNetwork&      network
        , const SearchSuccessor&          successor
        , std::optional<IntervalId>       interval
        , const SearchTimeDomain*         first_departure_domain
        , const TransferLimits&           limits
        , const SearchCostContext&        search_cost
        , PaperSuccessorStepConfig        config
        , PaperConnectionLabelRegistry&   label_registry
        , PaperConnectionNodeMetricMap&   paper_connections
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&      pruning_stats
    ) {
        if (!config.late_feasibility_prechecked) {
            const auto feasibility_decision =
                evaluate_paper_search_successor_feasibility(
                      branch
                    , network
                    , successor
                    , first_departure_domain
                    , limits
                );
            if (!feasibility_decision.accepted()) {
                return PaperSuccessorStepDecision{
                      .rejection = PaperSuccessorStepRejection::LateFeasibility
                    , .feasibility_rejection = feasibility_decision.rejection
                };
            }
        }

        std::optional<PaperConnectionLabelId> accepted_paper_label;
        std::optional<PaperConnectionRetentionDecision> retention_decision;
        if (config.retain_in_paper_c_y) {
            MATHFP_TRY_LET(
                  PaperConnectionPrefixEvaluation
                , paper_prefix
                , evaluate_paper_connection_prefix_before_branch(
                      branches
                    , branch
                    , network
                    , successor
                    , interval
                    , search_cost
                )
            );
            if (!paper_prefix.connection_candidate.has_value()) {
                if (paper_prefix.rejection == BranchTransitionRejection::RepeatedPhysicalNode
                    || paper_prefix.rejection == BranchTransitionRejection::RepeatedStopOccurrence) {
                    return PaperSuccessorStepDecision{
                          .rejection = PaperSuccessorStepRejection::PrefixCycle
                        , .prefix_rejection = paper_prefix.rejection
                    };
                }
                if (!paper_prefix.accepted()) {
                    return PaperSuccessorStepDecision{
                          .rejection = PaperSuccessorStepRejection::PrefixRejected
                        , .prefix_rejection = paper_prefix.rejection
                    };
                }
            } else {
                if (paper_prefix.connection_candidate->metrics.transfers
                    > limits.max_transfers) {
                    return PaperSuccessorStepDecision{
                        .rejection = PaperSuccessorStepRejection::PrefixTransferLimit
                    };
                }

                MATHFP_TRY_ASSIGN(
                    retention_decision,
                    retain_paper_connection_tree_node(
                          paper_prefix.connection_candidate->node
                        , std::move(paper_prefix.connection_candidate->metrics)
                        , branch.paper_connection_label
                        , label_registry
                        , paper_connections
                        , limits
                        , pruning_execution
                        , pruning_stats
                    )
                );
                if (!retention_decision->accepted()) {
                    return PaperSuccessorStepDecision{
                          .rejection = PaperSuccessorStepRejection::PrefixRetention
                        , .retention = std::move(retention_decision)
                    };
                }
                accepted_paper_label = retention_decision->label;
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
            return PaperSuccessorStepDecision{
                  .rejection = PaperSuccessorStepRejection::BranchCycle
                , .branch_rejection = transition.diagnostics.rejection
                , .retention = std::move(retention_decision)
            };
        }
        if (transition.branch->metrics.transfers > limits.max_transfers) {
            return PaperSuccessorStepDecision{
                  .rejection = PaperSuccessorStepRejection::BranchTransferLimit
                , .accepted_paper_label = accepted_paper_label
                , .retention = std::move(retention_decision)
            };
        }

        return PaperSuccessorStepDecision{
              .branch = std::move(transition.branch)
            , .accepted_paper_label = accepted_paper_label
            , .retention = std::move(retention_decision)
        };
    }

}  // namespace timetable::domain::assignment
