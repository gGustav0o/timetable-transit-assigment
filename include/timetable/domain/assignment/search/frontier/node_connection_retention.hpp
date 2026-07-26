#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/params/transfer_limits.hpp"

namespace timetable::domain::assignment {

    struct NodeConnectionRetentionDecision final {
        SearchPruningDecision pruning{};
        RetainedConnectionLabelId label{};
        std::vector<RetainedConnectionLabelId> removed_labels{};
        std::size_t removed_stale_labels{};

        [[nodiscard]] bool accepted() const noexcept {
            return pruning.accepted;
        }
    };

    /**
     * @brief Retain one candidate prefix in the node-local set C_y.
     *
     * This is the algebraic retention operation from the branch-and-
     * bound search. It owns only the C_y state, label liveness bookkeeping and
     * pruning diagnostics. It does not know about batches, logging, projection,
     * cancellation or frontier queues.
     */
    [[nodiscard]] mathfp::Expected<NodeConnectionRetentionDecision>
    retain_connection_tree_node(
          NodeConnectionSetKey            node
        , SearchPruningMetrics              metrics
        , std::optional<RetainedConnectionLabelId> parent_label
        , RetainedConnectionLabelRegistry&     label_registry
        , NodeConnectionSetMap&     node_connection_sets
        , const TransferLimits&             limits
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    );

}  // namespace timetable::domain::assignment
