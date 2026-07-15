#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"

#include <cstdint>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>

#include "timetable/domain/assignment/day_path.hpp"

namespace timetable::domain::assignment::runtime {

    [[nodiscard]] std::size_t& phase_counter(
          BranchPhaseStats& stats
        , SearchBranchPhase phase
    ) noexcept {
        switch (phase) {
            case SearchBranchPhase::AtOrigin:
                return stats.at_origin;

            case SearchBranchPhase::BeforeFirstBoarding:
                return stats.before_first_boarding;

            case SearchBranchPhase::AfterTimedRide:
                return stats.after_timed_ride;

            case SearchBranchPhase::AfterTransferWalk:
                return stats.after_transfer_walk;

            case SearchBranchPhase::Completed:
                return stats.completed;
        }

        return stats.completed;
    }

    void increment_walk_kind_stats(
          WalkKindStats&    stats
        , ConnectionLegKind kind
    ) noexcept {
        switch (kind) {
            case ConnectionLegKind::AccessWalk:
                ++stats.access;
                return;

            case ConnectionLegKind::TransferWalk:
                ++stats.transfer;
                return;

            case ConnectionLegKind::EgressWalk:
                ++stats.egress;
                return;

            case ConnectionLegKind::Ride:
            case ConnectionLegKind::InitialWait:
            case ConnectionLegKind::TransferWait:
            case ConnectionLegKind::FinalWait:
                return;
        }
    }

    void increment_phase_stats(
          BranchPhaseStats& stats
        , SearchBranchPhase phase
    ) noexcept {
        ++phase_counter(stats, phase);
    }

    void decrement_phase_stats(
          BranchPhaseStats& stats
        , SearchBranchPhase phase
    ) noexcept {
        auto& counter = phase_counter(stats, phase);
        if (counter > 0u) {
            --counter;
        }
    }

    std::string format_walk_kind_stats(
        const WalkKindStats& stats
    ) {
        return fmt::format(
              "access/transfer/egress={}/{}/{}"
            , stats.access
            , stats.transfer
            , stats.egress
        );
    }

    std::string format_walk_lookup_stats(
        const WalkLookupStats& stats
    ) {
        return fmt::format(
              "access/transfer/egress/skipped_phase/skipped_budget={}/{}/{}/{}/{}"
            , stats.access
            , stats.transfer
            , stats.egress
            , stats.skipped_by_phase
            , stats.skipped_by_transfer_budget
        );
    }

    std::string format_branch_phase_stats(
        const BranchPhaseStats& stats
    ) {
        return fmt::format(
              "origin/preboard/timed/transfer_walk/completed={}/{}/{}/{}/{}"
            , stats.at_origin
            , stats.before_first_boarding
            , stats.after_timed_ride
            , stats.after_transfer_walk
            , stats.completed
        );
    }

    std::size_t reachability_rejection_count(
        const ReachabilityRejectionStats& stats
    ) noexcept {
        return stats.phase
             + stats.transfer_budget
             + stats.unreachable_destination;
    }

    std::size_t suffix_lower_bound_rejection_count(
        const SuffixLowerBoundRejectionStats& stats
    ) noexcept {
        return stats.exact_dominance
             + stats.tolerance_impedance
             + stats.tolerance_journey_time
             + stats.tolerance_transfers;
    }

    mathfp::Expected<mathfp::Unit> validate_reachability_rejection_stats(
        const TaskSearchStats& stats
    ) {
        const auto detail_count = reachability_rejection_count(stats.reachability_rejections);
        if (detail_count != stats.rejected_reachability) {
            return mathfp::unexpected(
                mathfp::internal_error("reachability rejection diagnostics do not sum to total")
                    .ctx("total", static_cast<std::int64_t>(stats.rejected_reachability))
                    .ctx("detail", static_cast<std::int64_t>(detail_count))
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_suffix_lower_bound_rejection_stats(
        const TaskSearchStats& stats
    ) {
        const auto detail_count = suffix_lower_bound_rejection_count(
            stats.suffix_lower_bound_rejections
        );
        if (detail_count != stats.rejected_suffix_lower_bound) {
            return mathfp::unexpected(
                mathfp::internal_error("suffix lower-bound rejection diagnostics do not sum to total")
                    .ctx("total", static_cast<std::int64_t>(stats.rejected_suffix_lower_bound))
                    .ctx("detail", static_cast<std::int64_t>(detail_count))
            );
        }
        return mathfp::kUnit;
    }

    void add_reachability_rejection(
          TaskSearchStats&            stats
        , ReachabilityRejectionReason reason
    ) noexcept {
        ++stats.rejected_reachability;
        switch (reason) {
            case ReachabilityRejectionReason::Phase:
                ++stats.reachability_rejections.phase;
                return;
            case ReachabilityRejectionReason::TransferBudget:
                ++stats.reachability_rejections.transfer_budget;
                return;
            case ReachabilityRejectionReason::UnreachableDestination:
                ++stats.reachability_rejections.unreachable_destination;
                return;
        }
    }

    void add_suffix_lower_bound_rejection(
          TaskSearchStats&                 stats
        , SuffixLowerBoundRejectionReason reason
    ) noexcept {
        ++stats.rejected_suffix_lower_bound;
        switch (reason) {
            case SuffixLowerBoundRejectionReason::ExactDominance:
                ++stats.suffix_lower_bound_rejections.exact_dominance;
                return;
            case SuffixLowerBoundRejectionReason::ToleranceImpedance:
                ++stats.suffix_lower_bound_rejections.tolerance_impedance;
                return;
            case SuffixLowerBoundRejectionReason::ToleranceJourneyTime:
                ++stats.suffix_lower_bound_rejections.tolerance_journey_time;
                return;
            case SuffixLowerBoundRejectionReason::ToleranceTransfers:
                ++stats.suffix_lower_bound_rejections.tolerance_transfers;
                return;
        }
    }

    std::size_t retained_connection_count(
        std::span<const SearchProjectionRetention> retentions
    ) noexcept {
        std::size_t total = 0;
        for (const auto& retention : retentions) {
            total += retention.complete_connections.alternatives.size();
            total += retention.compact_complete_connections.metrics.size();
        }
        return total;
    }

    std::size_t retained_day_path_count(
        std::span<const SearchProjectionRetention> retentions
    ) noexcept {
        std::size_t total = 0;
        for (const auto& retention : retentions) {
            total += day_path_retention_size(retention.day_paths);
        }
        return total;
    }

    SearchStorageDiagnostics search_storage_diagnostics(
          const BranchArena&                         branches
        , std::size_t                                released_branches
        , std::size_t                                projection_state_count
        , std::size_t                                projection_state_size
        , const TreePartialRetention&                tree_retention
        , std::span<const SearchProjectionRetention> retentions
        , const SearchPruningRuntimeStats&           pruning_stats
    ) noexcept {
        const auto pruning_nodes = tree_retention.paper_connections.size();
        const auto pruning_buckets = tree_retention.paper_connections.bucket_count();
        std::size_t pruning_metrics = 0u;
        std::size_t pruning_labels = 0u;
        for (const auto& [node, metric_set] : tree_retention.paper_connections) {
            (void)node;
            pruning_metrics += metric_set.size();
            pruning_labels += metric_set.size();
        }
        const auto od_day_label_state_nodes = tree_retention.od_day_label_states.size();
        const auto od_day_label_state_buckets = tree_retention.od_day_label_states.bucket_count();
        std::size_t od_day_label_representatives = 0u;
        for (const auto& [state, representative_set] : tree_retention.od_day_label_states) {
            (void)state;
            od_day_label_representatives += representative_set.representatives.size();
        }
        const auto live_branches = branches.size() - released_branches;
        std::size_t compact_complete_metrics = 0;
        std::size_t day_path_alternatives = 0;
        for (const auto& retention : retentions) {
            compact_complete_metrics += retention.compact_complete_connections.metrics.size();
            day_path_alternatives += day_path_retention_size(retention.day_paths);
        }
        const auto approximate_direct_bytes =
              branches.size() * sizeof(BranchSlot)
            + live_branches * sizeof(SearchBranch)
            + branches.size() * (
                  sizeof(OdDayPathPrefixNode)
                + sizeof(OdDaySupportPrefixNode)
              )
            + projection_state_count * projection_state_size
            + pruning_nodes * sizeof(PaperConnectionNodeMetricMap::value_type)
            + pruning_buckets * sizeof(void*)
            + pruning_metrics * sizeof(SearchPruningMetrics)
            + pruning_labels * sizeof(PaperConnectionLabelId)
            + od_day_label_state_nodes * sizeof(OdDayLabelStateMap::value_type)
            + od_day_label_state_buckets * sizeof(void*)
            + od_day_label_representatives * sizeof(OdDayLabelRepresentative)
            + compact_complete_metrics * sizeof(CompleteConnectionMetrics)
            + day_path_alternatives * sizeof(DayPathAlternative);
        return SearchStorageDiagnostics{
              .branch_slots = branches.size()
            , .live_branches = live_branches
            , .released_branches = released_branches
            , .projection_states = projection_state_count
            , .tree_pruning_nodes = pruning_nodes
            , .tree_pruning_buckets = pruning_buckets
            , .tree_pruning_load_factor = tree_retention.paper_connections.load_factor()
            , .tree_pruning_metrics = pruning_metrics
            , .tree_pruning_labels = pruning_labels
            , .od_day_label_state_nodes = od_day_label_state_nodes
            , .od_day_label_state_buckets = od_day_label_state_buckets
            , .od_day_label_state_load_factor = tree_retention.od_day_label_states.load_factor()
            , .od_day_label_representatives = od_day_label_representatives
            , .tree_pruning_insertions = pruning_stats.inserted_metrics
            , .retained_complete_connections = retained_connection_count(retentions)
            , .retained_day_paths = retained_day_path_count(retentions)
            , .approximate_direct_bytes = approximate_direct_bytes
        };
    }

    std::string format_search_storage_diagnostics(
        const SearchStorageDiagnostics& diagnostics
    ) {
        return fmt::format(
              "search storage: branch_slots={} live_branches={} released_branches={} projection_states={} c_y(nodes/buckets/load/metrics/labels/insertions)={}/{}/{:.3f}/{}/{}/{} path_identity=compact_prefix support=compact_prefix legacy_od_day_label_states(nodes/buckets/load/reps)={}/{}/{:.3f}/{} retained_complete={} post_layer_day_paths={} approx_direct_mb={:.2f}"
            , diagnostics.branch_slots
            , diagnostics.live_branches
            , diagnostics.released_branches
            , diagnostics.projection_states
            , diagnostics.tree_pruning_nodes
            , diagnostics.tree_pruning_buckets
            , diagnostics.tree_pruning_load_factor
            , diagnostics.tree_pruning_metrics
            , diagnostics.tree_pruning_labels
            , diagnostics.tree_pruning_insertions
            , diagnostics.od_day_label_state_nodes
            , diagnostics.od_day_label_state_buckets
            , diagnostics.od_day_label_state_load_factor
            , diagnostics.od_day_label_representatives
            , diagnostics.retained_complete_connections
            , diagnostics.retained_day_paths
            , static_cast<double>(diagnostics.approximate_direct_bytes)
                / (1024.0 * 1024.0)
        );
    }

    std::string format_od_day_theory_diagnostics(
          const SearchStorageDiagnostics& diagnostics
        , const TaskSearchStats&          stats
        , std::size_t                     current_frontier
        , std::size_t                     next_frontier
    ) {
        return fmt::format(
              "OD-day theory diagnostics: paper=connection_segment_tree carrier=compact_connection_segment_prefix branch_projection_state=none reachability_prefilter=disabled_not_built reachability_masks=disabled c_y=network_node_known_connections c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink dominance=dep_arr_imp_nt tolerance=node_local tree_bounds=c_y_before_day_path_sink frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_contract=single_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available transfer_walk=first_class_segment composite_transfer_walk=disabled_in_production late_guard=assert_only day_path_retention=immediate_day_path_projection suffix_bound=disabled_for_od_day walk_lookup=lazy_phase_specific live_branches={} frontier={} projection_states={} c_y_nodes={} c_y_metrics={} c_y_labels={} c_y_removed(dominated/stale)={}/{} frontier_compaction(runs/removed/current/next)={}/{}/{}/{} stale_frontier_skipped={} post_layer_day_paths={} post_layer(candidates/inserted/replaced/max_supports)={}/{}/{}/{} enqueued_after_c_y={} suffix_bound_pruned_legacy={} c_y_pruned={} paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{} late_guard(time_domain/feasibility/reboarding)={}/{}/{} walk_lookup_counts({}) legacy_od_label_states={} legacy_label_reps={}"
            , diagnostics.live_branches
            , current_frontier + next_frontier
            , diagnostics.projection_states
            , diagnostics.tree_pruning_nodes
            , diagnostics.tree_pruning_metrics
            , diagnostics.tree_pruning_labels
            , stats.c_y_removed_dominated
            , stats.c_y_removed_stale
            , stats.frontier_compaction_runs
            , stats.frontier_compaction_removed
            , stats.frontier_compaction_removed_current
            , stats.frontier_compaction_removed_next
            , stats.stale_frontier_skipped
            , diagnostics.retained_day_paths
            , stats.post_layer_day_path_candidates
            , stats.post_layer_day_path_inserted
            , stats.post_layer_day_path_representative_replaced
            , stats.post_layer_day_path_supports
            , stats.accepted_branches
            , stats.rejected_suffix_lower_bound
            , stats.rejected_dominance_or_tolerance
            , stats.paper_timed_lookup_skipped_phase
            , stats.paper_timed_lookup_skipped_transfer_budget
            , stats.paper_timed_successor_rejected_time_domain
            , stats.paper_timed_successor_rejected_same_trip
            , stats.paper_timed_successor_rejected_same_line
            , stats.paper_timed_successor_rejected_feasibility
            , stats.rejected_time_domain
            , stats.rejected_feasibility
            , stats.rejected_reboarding
            , format_walk_lookup_stats(stats.walk_lookup)
            , diagnostics.od_day_label_state_nodes
            , diagnostics.od_day_label_representatives
        );
    }

    mathfp::Expected<mathfp::Unit> validate_od_day_production_batch_invariants(
          const SearchBatch&               batch
        , const TaskSearchStats&           stats
        , const SearchStorageDiagnostics&  storage
    ) {
        if (storage.projection_states != 0u) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production batch retained branch projection states")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("projection_states", static_cast<std::int64_t>(storage.projection_states))
            );
        }
        if (storage.retained_complete_connections != 0u) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production batch retained raw complete alternatives")
                    .ctx("origin", batch.key.origin.get())
                    .ctx(
                          "retained_complete_connections"
                        , static_cast<std::int64_t>(storage.retained_complete_connections)
                      )
            );
        }
        if (storage.tree_pruning_metrics != storage.tree_pruning_labels) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production C_y metrics and frontier labels are not synchronized")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("c_y_metrics", static_cast<std::int64_t>(storage.tree_pruning_metrics))
                    .ctx("c_y_labels", static_cast<std::int64_t>(storage.tree_pruning_labels))
            );
        }
        if (storage.od_day_label_state_nodes != 0u
            || storage.od_day_label_representatives != 0u) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production batch used legacy OD-day label storage")
                    .ctx("origin", batch.key.origin.get())
                    .ctx(
                          "legacy_label_nodes"
                        , static_cast<std::int64_t>(storage.od_day_label_state_nodes)
                      )
                    .ctx(
                          "legacy_label_representatives"
                        , static_cast<std::int64_t>(storage.od_day_label_representatives)
                      )
            );
        }
        if (stats.completed_connections != stats.post_layer_day_path_candidates) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day completed branches were not fully projected to DayPath post-layer")
                    .ctx("origin", batch.key.origin.get())
                    .ctx(
                          "completed_connections"
                        , static_cast<std::int64_t>(stats.completed_connections)
                      )
                    .ctx(
                          "post_layer_candidates"
                        , static_cast<std::int64_t>(stats.post_layer_day_path_candidates)
                      )
            );
        }
        if (stats.post_layer_day_path_inserted > stats.post_layer_day_path_candidates
            || storage.retained_day_paths > stats.post_layer_day_path_candidates) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day DayPath post-layer counters are inconsistent")
                    .ctx("origin", batch.key.origin.get())
                    .ctx(
                          "post_layer_candidates"
                        , static_cast<std::int64_t>(stats.post_layer_day_path_candidates)
                      )
                    .ctx(
                          "post_layer_inserted"
                        , static_cast<std::int64_t>(stats.post_layer_day_path_inserted)
                      )
                    .ctx(
                          "retained_day_paths"
                        , static_cast<std::int64_t>(storage.retained_day_paths)
                      )
            );
        }
        if (stats.rejected_reachability != 0u
            || stats.rejected_suffix_lower_bound != 0u) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production batch used disabled reachability/suffix pruning")
                    .ctx("origin", batch.key.origin.get())
                    .ctx(
                          "reachability_rejections"
                        , static_cast<std::int64_t>(stats.rejected_reachability)
                      )
                    .ctx(
                          "suffix_lower_bound_rejections"
                        , static_cast<std::int64_t>(stats.rejected_suffix_lower_bound)
                      )
            );
        }
        if (stats.rejected_time_domain != 0u
            || stats.rejected_feasibility != 0u
            || stats.rejected_reboarding != 0u) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production successor generator emitted non-insertable paper successor")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("late_time_domain", static_cast<std::int64_t>(stats.rejected_time_domain))
                    .ctx("late_feasibility", static_cast<std::int64_t>(stats.rejected_feasibility))
                    .ctx("late_reboarding", static_cast<std::int64_t>(stats.rejected_reboarding))
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_od_day_production_memory_limits(
          const SearchStorageDiagnostics& diagnostics
        , std::size_t                     current_frontier
        , std::size_t                     next_frontier
        , const OdDayProductionMemoryLimits& limits
        , ZoneId                          origin
    ) {
        const auto fail =
            [&](const char* metric, std::size_t actual, std::size_t limit)
                -> mathfp::Expected<mathfp::Unit> {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production memory limit exceeded")
                        .ctx("origin", origin.get())
                        .ctx("metric", std::string(metric))
                        .ctx("actual", static_cast<std::int64_t>(actual))
                        .ctx("limit", static_cast<std::int64_t>(limit))
                );
            };

        if (limits.max_branch_slots_per_tree.has_value()
            && diagnostics.branch_slots > *limits.max_branch_slots_per_tree) {
            return fail("branch_slots", diagnostics.branch_slots, *limits.max_branch_slots_per_tree);
        }
        if (limits.max_live_branches_per_tree.has_value()
            && diagnostics.live_branches > *limits.max_live_branches_per_tree) {
            return fail("live_branches", diagnostics.live_branches, *limits.max_live_branches_per_tree);
        }
        const auto frontier = current_frontier + next_frontier;
        if (limits.max_frontier_per_tree.has_value()
            && frontier > *limits.max_frontier_per_tree) {
            return fail("frontier", frontier, *limits.max_frontier_per_tree);
        }
        if (limits.max_od_day_label_states_per_tree.has_value()
            && diagnostics.od_day_label_state_nodes
                > *limits.max_od_day_label_states_per_tree) {
            return fail(
                  "od_day_label_states"
                , diagnostics.od_day_label_state_nodes
                , *limits.max_od_day_label_states_per_tree
            );
        }
        if (limits.max_retained_day_paths_per_tree.has_value()
            && diagnostics.retained_day_paths > *limits.max_retained_day_paths_per_tree) {
            return fail(
                  "post_layer_day_paths"
                , diagnostics.retained_day_paths
                , *limits.max_retained_day_paths_per_tree
            );
        }
        if (limits.max_approximate_direct_bytes_per_tree.has_value()
            && diagnostics.approximate_direct_bytes
                > *limits.max_approximate_direct_bytes_per_tree) {
            return fail(
                  "approximate_direct_bytes"
                , diagnostics.approximate_direct_bytes
                , *limits.max_approximate_direct_bytes_per_tree
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::runtime
