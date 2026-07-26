#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"
#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"

namespace timetable::domain::assignment {

    void update_node_connection_summary_with_metrics(
          SearchPruningSummary&       summary
        , const SearchPruningMetrics& metrics
    ) noexcept {
        if (summary.empty) {
            summary.min_impedance    = metrics.impedance;
            summary.min_journey_time = metrics.journey_time.value();
            summary.min_walk_time    = metrics.walk_time.value();
            summary.min_transfers    = static_cast<double>(metrics.transfers.get());
            summary.min_fare         = metrics.fare;
            summary.empty            = false;
            return;
        }
        summary.min_impedance = std::min(summary.min_impedance, metrics.impedance);
        summary.min_journey_time = std::min(
              summary.min_journey_time
            , metrics.journey_time.value()
        );
        summary.min_walk_time = std::min(
              summary.min_walk_time
            , metrics.walk_time.value()
        );
        summary.min_transfers = std::min(
              summary.min_transfers
            , static_cast<double>(metrics.transfers.get())
        );
        summary.min_fare = std::min(summary.min_fare, metrics.fare);
    }

    SearchPruningSummary summarize_connection_set_c_y(
        const ConnectionSetCyEntryVector& entries
    ) noexcept {
        SearchPruningSummary summary{};
        for (const auto& entry : entries) {
            update_node_connection_summary_with_metrics(summary, entry.metrics);
        }
        return summary;
    }

    std::size_t remove_inactive_node_connection_metrics(
          ConnectionSetCy&                    set
        , const RetainedConnectionLabelRegistry& registry
    ) {
        auto write = std::size_t{ 0u };
        for (std::size_t read = 0u; read < set.entries_.size(); ++read) {
            if (!retained_connection_label_active(
                  registry
                , std::optional<RetainedConnectionLabelId>{ set.entries_[read].label }
            )) {
                continue;
            }
            if (write != read) {
                set.entries_[write] = std::move(set.entries_[read]);
            }
            ++write;
        }
        const auto removed = set.entries_.size() - write;
        set.entries_.resize(write);
        if (removed != 0u) {
            set.summary_ = summarize_connection_set_c_y(set.entries_);
        }
        return removed;
    }

    std::size_t remove_inactive_node_connection_sets(
          NodeConnectionSetMap&       retention
        , const RetainedConnectionLabelRegistry& registry
    ) {
        auto removed = std::size_t{ 0u };
        for (auto it = retention.begin(); it != retention.end();) {
            removed += remove_inactive_node_connection_metrics(
                  it->second
                , registry
            );
            if (it->second.empty()) {
                it = retention.erase(it);
            } else {
                ++it;
            }
        }
        return removed;
    }

    mathfp::Expected<mathfp::Unit> validate_retained_connection_label_sync(
          const NodeConnectionSetMap& retention
        , const RetainedConnectionLabelRegistry& registry
        , ZoneId                              origin
    ) {
        for (const auto& [node, set] : retention) {
            for (const auto& entry : set.entries()) {
                if (!retained_connection_label_active(
                      registry
                    , std::optional<RetainedConnectionLabelId>{ entry.label }
                )) {
                    return mathfp::unexpected(
                        mathfp::internal_error("node-local C_y retained inactive frontier label")
                            .ctx("origin", origin.get())
                            .ctx("node_kind", static_cast<std::int64_t>(node.physical.kind))
                            .ctx("node_id", node.physical.id)
                            .ctx("label", static_cast<std::int64_t>(entry.label.value))
                    );
                }
            }
        }
        return mathfp::kUnit;
    }

    void insert_node_connection_metrics(
          const SearchPruningExecutionPlan& pruning_execution
        , ConnectionSetCy&                  set
        , SearchPruningMetrics              metrics
        , RetainedConnectionLabelId            label
        , std::vector<RetainedConnectionLabelId>& removed_labels
    ) {
        if (!stores_search_pruning_metrics(pruning_execution)) {
            return;
        }

        const auto dominated_begin = std::lower_bound(
              set.entries_.begin()
            , set.entries_.end()
            , metrics.arrival.value()
            , [](const ConnectionSetCyEntry& lhs, double arrival_value) {
                return lhs.metrics.arrival.value() < arrival_value;
            }
        );

        auto erase_pos = static_cast<std::size_t>(
            std::distance(set.entries_.begin(), dominated_begin)
        );
        auto removed_any = false;
        auto write_pos = erase_pos;
        for (auto read_pos = erase_pos; read_pos < set.entries_.size(); ++read_pos) {
            if (dominates_exactly(pruning_execution.exact_policy, metrics, set.entries_[read_pos].metrics)) {
                removed_labels.push_back(set.entries_[read_pos].label);
                removed_any = true;
                continue;
            }
            if (write_pos != read_pos) {
                set.entries_[write_pos] = std::move(set.entries_[read_pos]);
            }
            ++write_pos;
        }
        if (removed_any) {
            set.entries_.resize(write_pos);
        }

        const auto insertion = std::lower_bound(
              set.entries_.begin()
            , set.entries_.end()
            , metrics.arrival.value()
            , [](const ConnectionSetCyEntry& lhs, double arrival_value) {
                return lhs.metrics.arrival.value() < arrival_value;
            }
        );
        const auto inserted_metrics = metrics;
        set.entries_.insert(
              insertion
            , ConnectionSetCyEntry{
                  .metrics = std::move(metrics)
                , .label   = label
              }
        );
        if (removed_any) {
            set.summary_ = summarize_connection_set_c_y(set.entries_);
        } else {
            update_node_connection_summary_with_metrics(set.summary_, inserted_metrics);
        }
    }

    namespace {

        void insert_pruning_metrics(
              NodeMetricMap&                    known_metrics
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchNodeKey                     node
            , SearchPruningMetrics              metrics
        ) {
            auto& known = known_metrics[node];
            insert_search_pruning_metrics_in_place(
                  pruning_execution
                , known
                , std::move(metrics)
            );
        }

    }  // namespace

    mathfp::Expected<SearchPruningDecision> retain_branch(
          const SearchBranch&               branch
        , NodeMetricMap&                    known_metrics
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    ) {
        if (!branch.metrics.departure.has_value()
            || !branch.metrics.current_time.has_value()) {
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = SearchPruningReason::Accepted
                , .accepted = true
            };
        }

        ++pruning_stats.evaluated_candidates;
        MATHFP_TRY_LET(
              SearchPruningMetrics
            , metrics
            , make_partial_pruning_metrics(branch, search_cost)
        );
        const auto node = search_node_key(branch, pruning_execution);
        auto it = known_metrics.find(node);
        if (it == known_metrics.end()) {
            if (stores_search_pruning_metrics(pruning_execution)) {
                insert_pruning_metrics(
                      known_metrics
                    , pruning_execution
                    , node
                    , std::move(metrics)
                );
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = SearchPruningReason::Accepted
                , .accepted = true
            };
        }

        const auto pruning_decision = evaluate_search_pruning(
              pruning_execution
            , metrics
            , it->second
            , params.transfers
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
            return pruning_decision;
        }
        if (stores_search_pruning_metrics(pruning_execution)) {
            insert_pruning_metrics(
                  known_metrics
                , pruning_execution
                , node
                , std::move(metrics)
            );
            ++pruning_stats.inserted_metrics;
        } else {
            ++pruning_stats.skipped_insertions;
        }
        ++pruning_stats.accepted_candidates;
        return pruning_decision;
    }

    mathfp::Expected<SearchPruningDecision> retain_branch(
          const SearchBranch&               branch
        , SearchProjectionRetention&        retention
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    ) {
        return retain_branch(
              branch
            , retention.known_metrics
            , params
            , search_cost
            , pruning_execution
            , pruning_stats
        );
    }

    mathfp::Expected<SearchPruningDecision> retain_branch(
          const SearchBranch&               branch
        , TreePartialRetention&             retention
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    ) {
        return retain_branch(
              branch
            , retention.known_metrics
            , params
            , search_cost
            , pruning_execution
            , pruning_stats
        );
    }

    bool same_pruning_metrics(
          const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept {
        return lhs.departure.value() == rhs.departure.value()
            && lhs.arrival.value() == rhs.arrival.value()
            && lhs.journey_time.value() == rhs.journey_time.value()
            && lhs.walk_time.value() == rhs.walk_time.value()
            && lhs.transfers.get() == rhs.transfers.get()
            && lhs.fare == rhs.fare
            && lhs.impedance == rhs.impedance;
    }

    bool contains_pruning_metrics(
          const NodeMetricSet&        metric_set
        , const SearchPruningMetrics& metrics
    ) noexcept {
        return std::any_of(
              metric_set.metrics.begin()
            , metric_set.metrics.end()
            , [&](const SearchPruningMetrics& existing) {
                  return same_pruning_metrics(existing, metrics);
              }
        );
    }

}  // namespace timetable::domain::assignment
