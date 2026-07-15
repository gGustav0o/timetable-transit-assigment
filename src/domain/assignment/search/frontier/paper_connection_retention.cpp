#include "timetable/domain/assignment/search/frontier/paper_connection_retention.hpp"

#include <utility>

#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/relations/paper_connection_relevance.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<PaperConnectionRetentionDecision>
    retain_paper_connection_tree_node(
          PaperConnectionNodeKey            node
        , SearchPruningMetrics              metrics
        , std::optional<PaperConnectionLabelId> parent_label
        , PaperConnectionLabelRegistry&     label_registry
        , PaperConnectionNodeMetricMap&     paper_connections
        , const TransferLimits&             limits
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    ) {
        //tex:
        // Paper $$C_y$$ retention at the current tree node. A candidate
        // $$c_y^*=c_x^*+s^*_{x,y}$$ is accepted only if no retained
        // $$c\in C_y$$ dominates it by $$DEP,ARR,IMP,NT$$ and the node-local
        // tolerance bounds for $$IMP,JT,NT$$ and $$MAXNT$$ are satisfied.
        ++pruning_stats.evaluated_candidates;
        auto it = paper_connections.find(node);
        if (it == paper_connections.end()) {
            const auto label = allocate_paper_connection_label(
                  label_registry
                , parent_label
            );
            std::vector<PaperConnectionLabelId> removed_labels;
            if (stores_search_pruning_metrics(pruning_execution)) {
                auto& known = paper_connections[node];
                insert_paper_node_connection_metrics(
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
            return PaperConnectionRetentionDecision{
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
            remove_inactive_paper_node_connection_metrics(
                  it->second
                , label_registry
            );
        if (it->second.empty()) {
            const auto label = allocate_paper_connection_label(
                  label_registry
                , parent_label
            );
            std::vector<PaperConnectionLabelId> removed_labels;
            if (stores_search_pruning_metrics(pruning_execution)) {
                insert_paper_node_connection_metrics(
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
            return PaperConnectionRetentionDecision{
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

        const auto pruning_decision = evaluate_paper_node_connection_set(
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
            return PaperConnectionRetentionDecision{
                  .pruning = pruning_decision
                , .removed_stale_labels = removed_stale_labels
            };
        }

        const auto label = allocate_paper_connection_label(
              label_registry
            , parent_label
        );
        if (stores_search_pruning_metrics(pruning_execution)) {
            std::vector<PaperConnectionLabelId> removed_labels;
            insert_paper_node_connection_metrics(
                  pruning_execution
                , it->second
                , std::move(metrics)
                , label
                , removed_labels
            );
            ++pruning_stats.inserted_metrics;
            ++pruning_stats.accepted_candidates;
            return PaperConnectionRetentionDecision{
                  .pruning = pruning_decision
                , .label = label
                , .removed_labels = std::move(removed_labels)
                , .removed_stale_labels = removed_stale_labels
            };
        }

        ++pruning_stats.skipped_insertions;
        ++pruning_stats.accepted_candidates;
        return PaperConnectionRetentionDecision{
              .pruning = pruning_decision
            , .label = label
            , .removed_stale_labels = removed_stale_labels
        };
    }

}  // namespace timetable::domain::assignment
