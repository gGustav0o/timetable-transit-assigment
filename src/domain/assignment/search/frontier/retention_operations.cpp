#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment {

    std::span<const SearchPruningMetrics> paper_metric_span(
        const PaperNodeConnectionSet& set
    ) noexcept {
        return std::span<const SearchPruningMetrics>{
              set.metrics.data()
            , set.metrics.size()
        };
    }

    std::size_t remove_inactive_paper_node_connection_metrics(
          PaperNodeConnectionSet&             set
        , const PaperConnectionLabelRegistry& registry
    ) {
        auto write = std::size_t{ 0u };
        for (std::size_t read = 0u; read < set.metrics.size(); ++read) {
            if (read >= set.labels.size()) {
                continue;
            }
            if (!paper_connection_label_active(
                  registry
                , std::optional<PaperConnectionLabelId>{ set.labels[read] }
            )) {
                continue;
            }
            if (write != read) {
                set.metrics[write] = set.metrics[read];
                set.labels[write] = set.labels[read];
            }
            ++write;
        }
        const auto removed = set.metrics.size() - write;
        set.metrics.resize(write);
        set.labels.resize(write);
        if (removed != 0u) {
            set.summary = summarize_pruning_metrics(paper_metric_span(set));
        }
        return removed;
    }

    std::size_t remove_inactive_paper_connection_metrics(
          PaperConnectionNodeMetricMap&       retention
        , const PaperConnectionLabelRegistry& registry
    ) {
        auto removed = std::size_t{ 0u };
        for (auto it = retention.begin(); it != retention.end();) {
            removed += remove_inactive_paper_node_connection_metrics(
                  it->second
                , registry
            );
            if (it->second.metrics.empty()) {
                it = retention.erase(it);
            } else {
                ++it;
            }
        }
        return removed;
    }

    mathfp::Expected<mathfp::Unit> validate_paper_connection_label_sync(
          const PaperConnectionNodeMetricMap& retention
        , const PaperConnectionLabelRegistry& registry
        , ZoneId                              origin
    ) {
        for (const auto& [node, set] : retention) {
            if (set.metrics.size() != set.labels.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("paper C_y metric/label cardinality mismatch")
                        .ctx("origin", origin.get())
                        .ctx("node_kind", static_cast<std::int64_t>(node.physical.kind))
                        .ctx("node_id", node.physical.id)
                        .ctx("metrics", static_cast<std::int64_t>(set.metrics.size()))
                        .ctx("labels", static_cast<std::int64_t>(set.labels.size()))
                );
            }
            for (const auto label : set.labels) {
                if (!paper_connection_label_active(
                      registry
                    , std::optional<PaperConnectionLabelId>{ label }
                )) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper C_y retained inactive frontier label")
                            .ctx("origin", origin.get())
                            .ctx("node_kind", static_cast<std::int64_t>(node.physical.kind))
                            .ctx("node_id", node.physical.id)
                            .ctx("label", static_cast<std::int64_t>(label.value))
                    );
                }
            }
        }
        return mathfp::kUnit;
    }

    void update_paper_node_summary_with_metrics(
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

    void insert_paper_node_connection_metrics(
          const SearchPruningExecutionPlan& pruning_execution
        , PaperNodeConnectionSet&           set
        , SearchPruningMetrics              metrics
        , PaperConnectionLabelId            label
        , std::vector<PaperConnectionLabelId>& removed_labels
    ) {
        if (!stores_search_pruning_metrics(pruning_execution)) {
            return;
        }

        const auto dominated_begin = std::lower_bound(
              set.metrics.begin()
            , set.metrics.end()
            , metrics.arrival.value()
            , [](const SearchPruningMetrics& lhs, double arrival_value) {
                return lhs.arrival.value() < arrival_value;
            }
        );

        auto erase_pos = static_cast<std::size_t>(
            std::distance(set.metrics.begin(), dominated_begin)
        );
        auto removed_any = false;
        auto write_pos = erase_pos;
        for (auto read_pos = erase_pos; read_pos < set.metrics.size(); ++read_pos) {
            if (dominates_exactly(pruning_execution.exact_policy, metrics, set.metrics[read_pos])) {
                if (read_pos < set.labels.size()) {
                    removed_labels.push_back(set.labels[read_pos]);
                }
                removed_any = true;
                continue;
            }
            if (write_pos != read_pos) {
                set.metrics[write_pos] = std::move(set.metrics[read_pos]);
                if (read_pos < set.labels.size() && write_pos < set.labels.size()) {
                    set.labels[write_pos] = set.labels[read_pos];
                }
            }
            ++write_pos;
        }
        if (removed_any) {
            set.metrics.resize(write_pos);
            set.labels.resize(write_pos);
        }

        const auto insertion = std::lower_bound(
              set.metrics.begin()
            , set.metrics.end()
            , metrics.arrival.value()
            , [](const SearchPruningMetrics& lhs, double arrival_value) {
                return lhs.arrival.value() < arrival_value;
            }
        );
        const auto inserted_metrics = metrics;
        const auto insertion_pos = static_cast<std::size_t>(
            std::distance(set.metrics.begin(), insertion)
        );
        set.metrics.insert(insertion, std::move(metrics));
        set.labels.insert(
              set.labels.begin() + static_cast<std::ptrdiff_t>(insertion_pos)
            , label
        );
        if (removed_any) {
            set.summary = summarize_pruning_metrics(paper_metric_span(set));
        } else {
            update_paper_node_summary_with_metrics(set.summary, inserted_metrics);
        }
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

    bool contains_od_day_label_representative(
          const OdDayLabelRepresentativeSet& representatives
        , const SearchPruningMetrics&        metrics
        , const TimedSupportEnvelope&        support
    ) noexcept {
        return std::any_of(
              representatives.representatives.begin()
            , representatives.representatives.end()
            , [&](const OdDayLabelRepresentative& existing) {
                  return same_pruning_metrics(existing.metrics, metrics)
                      && existing.support == support;
              }
        );
    }

    bool timed_support_envelope_covers(
          const TimedSupportEnvelope& existing
        , const TimedSupportEnvelope& candidate
    ) noexcept {
        if (existing.key != candidate.key) {
            return false;
        }
        if (candidate.labels.empty()) {
            return existing.labels.empty();
        }
        return std::all_of(
              candidate.labels.begin()
            , candidate.labels.end()
            , [&](const TimedSupportLabel& label) {
                  return std::find(
                        existing.labels.begin()
                      , existing.labels.end()
                      , label
                  ) != existing.labels.end();
              }
        );
    }

    SearchPruningMetricSet compatible_od_day_label_metrics(
          const OdDayLabelRepresentativeSet& representatives
        , const TimedSupportEnvelope&        support
    ) {
        SearchPruningMetricSet metric_set;
        for (const auto& representative : representatives.representatives) {
            if (timed_support_envelope_covers(representative.support, support)) {
                metric_set.metrics.push_back(representative.metrics);
            }
        }
        std::sort(
              metric_set.metrics.begin()
            , metric_set.metrics.end()
            , [](const SearchPruningMetrics& lhs, const SearchPruningMetrics& rhs) {
                  return lhs.arrival.value() < rhs.arrival.value();
              }
        );
        metric_set.summary = summarize_pruning_metrics(
            std::span<const SearchPruningMetrics>{
                  metric_set.metrics.data()
                , metric_set.metrics.size()
            }
        );
        return metric_set;
    }

    void refresh_od_day_label_summary_metrics(
        OdDayLabelRepresentativeSet& representatives
    ) {
        representatives.summary_metrics.metrics.clear();
        representatives.summary_metrics.metrics.reserve(representatives.representatives.size());
        for (const auto& representative : representatives.representatives) {
            representatives.summary_metrics.metrics.push_back(representative.metrics);
        }
        std::sort(
              representatives.summary_metrics.metrics.begin()
            , representatives.summary_metrics.metrics.end()
            , [](const SearchPruningMetrics& lhs, const SearchPruningMetrics& rhs) {
                  return lhs.arrival.value() < rhs.arrival.value();
              }
        );
        representatives.summary_metrics.summary = summarize_pruning_metrics(
            std::span<const SearchPruningMetrics>{
                  representatives.summary_metrics.metrics.data()
                , representatives.summary_metrics.metrics.size()
            }
        );
    }

    void insert_od_day_label_representative(
          const SearchPruningExecutionPlan& pruning_execution
        , OdDayLabelRepresentativeSet&      representatives
        , const SearchPruningMetrics&       metrics
        , const TimedSupportEnvelope&       support
    ) {
        if (pruning_execution.exact_enabled) {
            representatives.representatives.erase(
                  std::remove_if(
                        representatives.representatives.begin()
                      , representatives.representatives.end()
                      , [&](const OdDayLabelRepresentative& existing) {
                      return timed_support_envelope_covers(support, existing.support)
                          && dominates_exactly(
                                pruning_execution.exact_policy
                              , metrics
                              , existing.metrics
                          );
                      }
                  )
                , representatives.representatives.end()
            );
        }
        representatives.representatives.push_back(
            OdDayLabelRepresentative{
                  .metrics = metrics
                , .support = support
            }
        );
        refresh_od_day_label_summary_metrics(representatives);
    }

}  // namespace timetable::domain::assignment
