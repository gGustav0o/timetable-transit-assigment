#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <optional>
#include <utility>

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment {

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

    SearchPruningSummary summarize_connection_set_c_y(
        const ConnectionSetCyEntryVector& entries
    ) noexcept {
        SearchPruningSummary summary{};
        for (const auto& entry : entries) {
            update_paper_node_summary_with_metrics(summary, entry.metrics);
        }
        return summary;
    }

    std::size_t remove_inactive_paper_node_connection_metrics(
          ConnectionSetCy&                    set
        , const PaperConnectionLabelRegistry& registry
    ) {
        auto write = std::size_t{ 0u };
        for (std::size_t read = 0u; read < set.entries_.size(); ++read) {
            if (!paper_connection_label_active(
                  registry
                , std::optional<PaperConnectionLabelId>{ set.entries_[read].label }
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
            if (it->second.empty()) {
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
            for (const auto& entry : set.entries()) {
                if (!paper_connection_label_active(
                      registry
                    , std::optional<PaperConnectionLabelId>{ entry.label }
                )) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper C_y retained inactive frontier label")
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

    void insert_paper_node_connection_metrics(
          const SearchPruningExecutionPlan& pruning_execution
        , ConnectionSetCy&                  set
        , SearchPruningMetrics              metrics
        , PaperConnectionLabelId            label
        , std::vector<PaperConnectionLabelId>& removed_labels
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
            update_paper_node_summary_with_metrics(set.summary_, inserted_metrics);
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
