#include "timetable/domain/assignment/search/frontier/node_connection_retention.hpp"

#include <utility>

#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/relations/node_connection_relevance.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<NodeConnectionRetentionDecision>
    retain_connection_tree_node(
          NodeConnectionSetKey            node
        , SearchPruningMetrics              metrics
        , std::optional<RetainedConnectionLabelId> parent_label
        , RetainedConnectionLabelRegistry&     label_registry
        , NodeConnectionSetMap&     node_connection_sets
        , const TransferLimits&             limits
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    ) {
        //tex:
        // Node-local $$C_y$$ retention at the current tree node. A candidate
        // $$c_y^*=c_x^*+s^*_{x,y}$$ is accepted only if no retained
        // $$c\in C_y$$ dominates it by $$DEP,ARR,IMP,NT$$ and the node-local
        // tolerance bounds for $$IMP,JT,NT$$ and $$MAXNT$$ are satisfied.
        ++pruning_stats.evaluated_candidates;
        auto it = node_connection_sets.find(node);
        if (it == node_connection_sets.end()) {
            const auto label = allocate_retained_connection_label(
                  label_registry
                , parent_label
            );
            std::vector<RetainedConnectionLabelId> removed_labels;
            if (stores_search_pruning_metrics(pruning_execution)) {
                auto& known = node_connection_sets[node];
                insert_node_connection_metrics(
                      pruning_execution
                    , known
                    , std::move(metrics)
                    , label
                    , removed_labels
                );
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return NodeConnectionRetentionDecision{
                  .pruning = SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                  }
                , .label = label
                , .removed_labels = std::move(removed_labels)
            };
        }

        const auto removed_stale_labels =
            remove_inactive_node_connection_metrics(
                  it->second
                , label_registry
            );
        if (it->second.empty()) {
            const auto label = allocate_retained_connection_label(
                  label_registry
                , parent_label
            );
            std::vector<RetainedConnectionLabelId> removed_labels;
            if (stores_search_pruning_metrics(pruning_execution)) {
                insert_node_connection_metrics(
                      pruning_execution
                    , it->second
                    , std::move(metrics)
                    , label
                    , removed_labels
                );
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return NodeConnectionRetentionDecision{
                  .pruning = SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                  }
                , .label = label
                , .removed_labels = std::move(removed_labels)
                , .removed_stale_labels = removed_stale_labels
            };
        }

        const auto pruning_decision = evaluate_node_connection_set(
              pruning_execution
            , metrics
            , it->second
            , limits
        );
        if (!pruning_decision.accepted) {
            switch (pruning_decision.layer) {
                case SearchPruningLayer::Exact:
                    ++pruning_stats.rejected_exact;
                    break;
                case SearchPruningLayer::Approximate:
                    ++pruning_stats.rejected_approximate;
                    break;
            }
            return NodeConnectionRetentionDecision{
                  .pruning = pruning_decision
                , .removed_stale_labels = removed_stale_labels
            };
        }

        const auto label = allocate_retained_connection_label(
              label_registry
            , parent_label
        );
        if (stores_search_pruning_metrics(pruning_execution)) {
            std::vector<RetainedConnectionLabelId> removed_labels;
            insert_node_connection_metrics(
                  pruning_execution
                , it->second
                , std::move(metrics)
                , label
                , removed_labels
            );
            ++pruning_stats.inserted_metrics;
            ++pruning_stats.accepted_candidates;
            return NodeConnectionRetentionDecision{
                  .pruning = pruning_decision
                , .label = label
                , .removed_labels = std::move(removed_labels)
                , .removed_stale_labels = removed_stale_labels
            };
        }

        ++pruning_stats.skipped_insertions;
        ++pruning_stats.accepted_candidates;
        return NodeConnectionRetentionDecision{
              .pruning = pruning_decision
            , .label = label
            , .removed_stale_labels = removed_stale_labels
        };
    }

}  // namespace timetable::domain::assignment
