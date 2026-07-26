#pragma once

#include <cstddef>
#include <optional>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/frontier/node_connection_retention.hpp"
#include "timetable/domain/assignment/search/generation/branch_transition.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/params/transfer_limits.hpp"

namespace timetable::domain::assignment {

    enum class TreeSuccessorStepRejection {
          None
        , LateFeasibility
        , PrefixCycle
        , PrefixRejected
        , PrefixTransferLimit
        , PrefixRetention
        , BranchCycle
        , BranchTransferLimit
    };

    struct TreeSuccessorStepConfig final {
        bool late_feasibility_prechecked{};
        bool retain_in_node_connection_sets{};
    };

    struct TreeSuccessorStepDecision final {
        std::optional<SearchBranch> branch{};
        std::optional<RetainedConnectionLabelId> accepted_retained_label{};
        TreeSuccessorStepRejection rejection{ TreeSuccessorStepRejection::None };
        SuccessorFeasibilityRejection feasibility_rejection{
            SuccessorFeasibilityRejection::None
        };
        BranchTransitionRejection prefix_rejection{
            BranchTransitionRejection::None
        };
        BranchTransitionRejection branch_rejection{
            BranchTransitionRejection::None
        };
        std::optional<NodeConnectionRetentionDecision> retention{};

        [[nodiscard]] bool accepted() const noexcept {
            return rejection == TreeSuccessorStepRejection::None
                && branch.has_value();
        }
    };

    /**
     * @brief Pure branch-and-bound successor acceptance step.
     *
     * The step decides whether one generated successor becomes the next search
     * branch. It owns late feasibility checks, optional node-local C_y retention and
     * structural branch transition. It intentionally does not project complete
     * connections, mutate frontier queues, log, cancel, or inspect batch shape.
     */
    [[nodiscard]] mathfp::Expected<TreeSuccessorStepDecision>
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
    );

}  // namespace timetable::domain::assignment
