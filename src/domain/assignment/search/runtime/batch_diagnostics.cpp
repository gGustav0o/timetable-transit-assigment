#include "timetable/domain/assignment/search/runtime/batch_diagnostics.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <fmt/format.h>

#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        constexpr auto kSearchWallClockHeartbeatInterval =
            std::chrono::seconds{ 30 };

        [[nodiscard]] std::string format_batch_interval(
            const std::optional<IntervalId>& interval
        ) {
            if (!interval.has_value()) {
                return "period";
            }
            return std::to_string(interval->get());
        }

        [[nodiscard]] std::string format_optional_size_limit(
            std::optional<std::size_t> limit
        ) {
            return limit.has_value()
                ? std::to_string(*limit)
                : std::string("unbounded");
        }

        [[nodiscard]] std::string format_optional_mb_limit(
            std::optional<std::size_t> limit
        ) {
            return limit.has_value()
                ? fmt::format("{:.2f}", static_cast<double>(*limit) / (1024.0 * 1024.0))
                : std::string("unbounded");
        }

        [[nodiscard]] std::size_t projection_state_size(
            const SearchBatchContext& context
        ) noexcept {
            if (context.fixed.od_day_slots) {
                return std::size_t{ 0u };
            }
            return context.fixed.target_projection_slots
                ? sizeof(FixedActiveMask)
                : sizeof(DemandBranchProjectionState);
        }

    }  // namespace

    SearchBatchDiagnosticsRuntime::SearchBatchDiagnosticsRuntime(
        SearchBatchContext& context
    )
        : context_{ context }
        , batch_started_at_{ std::chrono::steady_clock::now() }
        , last_wall_clock_heartbeat_{ batch_started_at_ }
    {}

    std::size_t
    SearchBatchDiagnosticsRuntime::retained_production_alternative_count()
        const noexcept {
        const auto& fixed = context_.fixed;
        const auto& state = context_.mutable_state;
        return fixed.od_day_slots
            ? retained_day_path_count(state.retentions)
            : retained_connection_count(state.retentions);
    }

    std::size_t SearchBatchDiagnosticsRuntime::projection_state_count()
        const noexcept {
        const auto& fixed = context_.fixed;
        const auto& state = context_.mutable_state;
        if (fixed.od_day_slots) {
            return std::size_t{ 0u };
        }
        return fixed.target_projection_slots
            ? state.completion_projection_states.size()
                - state.released_branches
            : state.demand_projection_states.size()
                - state.released_branches;
    }

    SearchStorageDiagnostics
    SearchBatchDiagnosticsRuntime::storage_diagnostics() const noexcept {
        const auto& state = context_.mutable_state;
        return search_storage_diagnostics(
              state.branches
            , state.released_branches
            , projection_state_count()
            , projection_state_size(context_)
            , state.tree_partial_retention
            , std::span<const SearchProjectionRetention>{
                  state.retentions.data()
                , state.retentions.size()
              }
            , state.stats.pruning
        );
    }

    void SearchBatchDiagnosticsRuntime::emit_initial_status() const {
        const auto& fixed = context_.fixed;
        timetable::infra::progress::status(
            fmt::format(
                  "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} frontier={} found={}"
                , fixed.batch_index + 1
                , fixed.batch_count
                , fixed.batch.key.origin.get()
                , format_batch_interval(fixed.batch.key.interval)
                , fixed.batch.projection_slots.size()
                , fixed.batch.completion_targets.size()
                , fixed.diagnostics.capacity_iteration
                , context_.mutable_state.level_expansion.current_frontier.size()
                , retained_production_alternative_count()
            )
        );
    }

    void SearchBatchDiagnosticsRuntime::emit_od_day_memory_limits(
        const OdDayProductionMemoryLimits& limits
    ) const {
        if (!context_.fixed.od_day_slots) {
            return;
        }
        timetable::infra::progress::log(
            fmt::format(
                  "OD-day production memory limits: carrier=compact_connection_segment_prefix branch_slots={} live_branches={} frontier={} label_representatives=unbounded post_layer_day_paths={} approx_direct_mb={}"
                , format_optional_size_limit(limits.max_branch_slots_per_tree)
                , format_optional_size_limit(limits.max_live_branches_per_tree)
                , format_optional_size_limit(limits.max_frontier_per_tree)
                , format_optional_size_limit(limits.max_retained_day_paths_per_tree)
                , format_optional_mb_limit(limits.max_approximate_direct_bytes_per_tree)
            )
            , timetable::infra::LogLevel::Info
        );
    }

    void SearchBatchDiagnosticsRuntime::emit_storage_diagnostics() const {
        const auto diagnostics = storage_diagnostics();
        timetable::infra::progress::log(
              format_search_storage_diagnostics(diagnostics)
            , timetable::infra::LogLevel::Info
        );
        if (context_.fixed.od_day_slots) {
            const auto& expansion = context_.mutable_state.level_expansion;
            timetable::infra::progress::log(
                format_od_day_theory_diagnostics(
                      diagnostics
                    , context_.mutable_state.stats
                    , expansion.current_frontier.size()
                    , expansion.next_frontier.size()
                )
                , timetable::infra::LogLevel::Info
            );
        }
    }

    void SearchBatchDiagnosticsRuntime::emit_wall_clock_heartbeat(
          const char* stage
        , std::size_t branch_index
    ) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_wall_clock_heartbeat_
            < kSearchWallClockHeartbeatInterval) {
            return;
        }
        last_wall_clock_heartbeat_ = now;
        const auto elapsed_ms = std::chrono::duration_cast<
            std::chrono::milliseconds
        >(now - batch_started_at_).count();
        const auto& fixed = context_.fixed;
        const auto& state = context_.mutable_state;
        const auto& expansion = state.level_expansion;
        timetable::infra::progress::log(
            fmt::format(
                  "search wall heartbeat: batch={:>8} origin={:>4} interval={:>4}"
                  " tasks={:>5} targets={:>5} capacity_iteration={:>4} stage={} branch={} elapsed_ms={}"
                  " expanded={:>8} generated={:>8} accepted={:>8} found={:>8}"
                  " frontier={}/{}"
                  " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={})"
                  " frontier_phase(current={}, next={})"
                  " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                  " accepted_phase({})"
                , static_cast<std::int64_t>(fixed.batch_index)
                , fixed.batch.key.origin.get()
                , format_batch_interval(fixed.batch.key.interval)
                , fixed.batch.projection_slots.size()
                , fixed.batch.completion_targets.size()
                , fixed.diagnostics.capacity_iteration
                , stage
                , static_cast<std::int64_t>(branch_index)
                , static_cast<std::int64_t>(elapsed_ms)
                , state.stats.expanded_branches
                , state.stats.generated_successors
                , state.stats.accepted_branches
                , retained_production_alternative_count()
                , expansion.current_frontier.size()
                , expansion.next_frontier.size()
                , state.stats.stale_frontier_skipped
                , state.stats.c_y_removed_dominated
                , state.stats.c_y_removed_stale
                , state.stats.frontier_compaction_runs
                , state.stats.frontier_compaction_removed
                , format_branch_phase_stats(expansion.current_frontier_by_phase)
                , format_branch_phase_stats(expansion.next_frontier_by_phase)
                , format_walk_lookup_stats(state.stats.walk_lookup)
                , format_walk_kind_stats(state.stats.generated_walk)
                , format_walk_kind_stats(state.stats.accepted_walk)
                , state.stats.rejected_consecutive_walk
                , format_branch_phase_stats(state.stats.accepted_branches_by_phase)
            )
            , timetable::infra::LogLevel::Info
        );
        emit_storage_diagnostics();
    }

    void SearchBatchDiagnosticsRuntime::emit_search_heartbeat() const {
        const auto& fixed = context_.fixed;
        const auto& state = context_.mutable_state;
        const auto& expansion = state.level_expansion;
        timetable::infra::progress::status(
            fmt::format(
                  "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} expanded={} accepted={} found={} frontier={}/{}"
                , fixed.batch_index + 1
                , fixed.batch_count
                , fixed.batch.key.origin.get()
                , format_batch_interval(fixed.batch.key.interval)
                , fixed.batch.projection_slots.size()
                , fixed.batch.completion_targets.size()
                , fixed.diagnostics.capacity_iteration
                , state.stats.expanded_branches
                , state.stats.accepted_branches
                , retained_production_alternative_count()
                , expansion.current_frontier.size()
                , expansion.next_frontier.size()
            )
        );
        timetable::infra::progress::log(
            fmt::format(
                  "search heartbeat: batch={:>8} origin={:>4} interval={:>4} tasks={:>5} targets={:>5} capacity_iteration={:>4} expanded={:>8} generated={:>8}"
                  " accepted={:>8} found={:>8} rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                  " lower_bound_pruned={}"
                  " pruning(exact/approx/inserted/skipped)={}/{}/{}/{}"
                  " frontier={}/{}"
                  " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={})"
                  " paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{}"
                  " late_guard(time_domain/feasibility/reboarding)={}/{}/{}"
                  " frontier_phase(current={}, next={})"
                  " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                  " accepted_phase({})"
                , static_cast<std::int64_t>(fixed.batch_index)
                , fixed.batch.key.origin.get()
                , format_batch_interval(fixed.batch.key.interval)
                , fixed.batch.projection_slots.size()
                , fixed.batch.completion_targets.size()
                , fixed.diagnostics.capacity_iteration
                , state.stats.expanded_branches
                , state.stats.generated_successors
                , state.stats.accepted_branches
                , retained_production_alternative_count()
                , state.stats.rejected_time_domain
                , state.stats.rejected_feasibility
                , state.stats.rejected_reboarding
                , state.stats.rejected_cycles
                , state.stats.rejected_transfer_limit
                , state.stats.rejected_reachability
                , state.stats.rejected_dominance_or_tolerance
                , state.stats.rejected_suffix_lower_bound
                , state.stats.pruning.rejected_exact
                , state.stats.pruning.rejected_approximate
                , state.stats.pruning.inserted_metrics
                , state.stats.pruning.skipped_insertions
                , expansion.current_frontier.size()
                , expansion.next_frontier.size()
                , state.stats.stale_frontier_skipped
                , state.stats.c_y_removed_dominated
                , state.stats.c_y_removed_stale
                , state.stats.frontier_compaction_runs
                , state.stats.frontier_compaction_removed
                , state.stats.paper_timed_lookup_skipped_phase
                , state.stats.paper_timed_lookup_skipped_transfer_budget
                , state.stats.paper_timed_successor_rejected_time_domain
                , state.stats.paper_timed_successor_rejected_same_trip
                , state.stats.paper_timed_successor_rejected_same_line
                , state.stats.paper_timed_successor_rejected_feasibility
                , state.stats.rejected_time_domain
                , state.stats.rejected_feasibility
                , state.stats.rejected_reboarding
                , format_branch_phase_stats(expansion.current_frontier_by_phase)
                , format_branch_phase_stats(expansion.next_frontier_by_phase)
                , format_walk_lookup_stats(state.stats.walk_lookup)
                , format_walk_kind_stats(state.stats.generated_walk)
                , format_walk_kind_stats(state.stats.accepted_walk)
                , state.stats.rejected_consecutive_walk
                , format_branch_phase_stats(state.stats.accepted_branches_by_phase)
            )
            , timetable::infra::LogLevel::Info
        );
        timetable::infra::progress::log(
              format_search_pruning_runtime_stats(summarize(state.stats.pruning))
            , timetable::infra::LogLevel::Info
        );
        emit_storage_diagnostics();
    }

    void SearchBatchDiagnosticsRuntime::emit_batch_cancelled_before_tree() const {
        const auto& fixed = context_.fixed;
        const auto& state = context_.mutable_state;
        timetable::infra::progress::log(
            fmt::format(
                  "search batch cancelled: {}/{} origin={} interval={} expanded={} accepted={} found={} reason=sibling_failed"
                , fixed.batch_index + 1
                , fixed.batch_count
                , fixed.batch.key.origin.get()
                , format_batch_interval(fixed.batch.key.interval)
                , state.stats.expanded_branches
                , state.stats.accepted_branches
                , retained_production_alternative_count()
            )
            , timetable::infra::LogLevel::Warning
        );
    }

    void SearchBatchDiagnosticsRuntime::emit_batch_cancelled_during_successor_scan()
        const {
        const auto& fixed = context_.fixed;
        const auto& state = context_.mutable_state;
        timetable::infra::progress::log(
            fmt::format(
                  "search batch cancelled during successor scan: {}/{} origin={} interval={} expanded={} accepted={} found={} reason=sibling_failed"
                , fixed.batch_index + 1
                , fixed.batch_count
                , fixed.batch.key.origin.get()
                , format_batch_interval(fixed.batch.key.interval)
                , state.stats.expanded_branches
                , state.stats.accepted_branches
                , retained_production_alternative_count()
            )
            , timetable::infra::LogLevel::Warning
        );
    }

    void SearchBatchDiagnosticsRuntime::emit_frontier_compacted(
          const char* reason
        , const OdDayFrontierCompactionResult& compaction
    ) const {
        if (compaction.removed() == 0u) {
            return;
        }
        const auto& fixed = context_.fixed;
        const auto& expansion = context_.mutable_state.level_expansion;
        timetable::infra::progress::log(
            fmt::format(
                  "OD-day frontier compacted: origin={} reason={} removed={} current_removed={} next_removed={} frontier={}/{}"
                , fixed.batch.key.origin.get()
                , reason
                , compaction.removed()
                , compaction.removed_current
                , compaction.removed_next
                , expansion.current_frontier.size()
                , expansion.next_frontier.size()
            )
            , timetable::infra::LogLevel::Debug
        );
    }

    void SearchBatchDiagnosticsRuntime::emit_projection_details(
        const SearchBatchFinalization& finalization
    ) const {
        const auto& fixed = context_.fixed;
        const auto& task_stats = context_.mutable_state.task_stats;
        if (!fixed.diagnostics.log_projection_details) {
            return;
        }
        for (std::size_t task_pos = 0; task_pos < finalization.slot_results.size(); ++task_pos) {
            const auto& slot_result = finalization.slot_results[task_pos];
            const auto& slot = slot_result.slot;
            const auto before_tolerance =
                finalization.slot_finalizations[task_pos]
                    .retained_before_tolerance;
            timetable::infra::progress::log(
                fmt::format(
                      "search batch projection done: batch={}/{} slot={} kind={} task={} origin={} destination={} interval={} found={:>8}"
                      " retained_before_tolerance={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                      " reachability_pruned={} reachability_detail(phase/budget/unreachable)={}/{}/{}"
                      " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                    , fixed.batch_index + 1
                    , fixed.batch_count
                    , static_cast<std::int64_t>(task_pos)
                    , to_log_token(slot.kind)
                    , slot.task_ref.has_value()
                        ? std::to_string(slot.task_ref->get())
                        : std::string{"<none>"}
                    , slot.origin.get()
                    , slot.destination.get()
                    , slot.interval.has_value()
                        ? std::to_string(slot.interval->get())
                        : std::string{"<none>"}
                    , slot_result.connection_count
                    , before_tolerance
                    , task_stats[task_pos].rejected_complete_admissibility
                    , task_stats[task_pos].rejected_complete_dominance
                    , task_stats[task_pos].rejected_complete_tolerance
                    , task_stats[task_pos].removed_complete_dominated
                    , task_stats[task_pos].rejected_reachability
                    , task_stats[task_pos].reachability_rejections.phase
                    , task_stats[task_pos].reachability_rejections.transfer_budget
                    , task_stats[task_pos].reachability_rejections.unreachable_destination
                    , task_stats[task_pos].rejected_suffix_lower_bound
                    , task_stats[task_pos].suffix_lower_bound_rejections.exact_dominance
                    , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_impedance
                    , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_journey_time
                    , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_transfers
                )
                , timetable::infra::LogLevel::Info
            );
        }
    }

    void SearchBatchDiagnosticsRuntime::emit_batch_done(
        const SearchBatchFinalization& finalization
    ) const {
        const auto& fixed = context_.fixed;
        const auto& state = context_.mutable_state;
        timetable::infra::progress::log(
            fmt::format(
                  "search batch done: {}/{} origin={} interval={} tasks={} found={:>8} completed={:>8}"
                  " retained_before_tolerance={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                  " expanded={:>8} generated={:>8} accepted={:>8}"
                  " rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                  " reachability_detail(phase/budget/unreachable)={}/{}/{} max_frontier={}/{}"
                  " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                  " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={} compact_current={} compact_next={})"
                  " post_layer(candidates/inserted/replaced/max_supports)={}/{}/{}/{}"
                  " paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{}"
                  " late_guard(time_domain/feasibility/reboarding)={}/{}/{}"
                  " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                  " accepted_phase({})"
                , fixed.batch_index + 1
                , fixed.batch_count
                , fixed.batch.key.origin.get()
                , format_batch_interval(fixed.batch.key.interval)
                , fixed.batch.projection_slots.size()
                , finalization.final_found
                , state.stats.completed_connections
                , finalization.retained_before_tolerance
                , state.stats.rejected_complete_admissibility
                , state.stats.rejected_complete_dominance
                , state.stats.rejected_complete_tolerance
                , state.stats.removed_complete_dominated
                , state.stats.expanded_branches
                , state.stats.generated_successors
                , state.stats.accepted_branches
                , state.stats.rejected_time_domain
                , state.stats.rejected_feasibility
                , state.stats.rejected_reboarding
                , state.stats.rejected_cycles
                , state.stats.rejected_transfer_limit
                , state.stats.rejected_reachability
                , state.stats.rejected_dominance_or_tolerance
                , state.stats.reachability_rejections.phase
                , state.stats.reachability_rejections.transfer_budget
                , state.stats.reachability_rejections.unreachable_destination
                , state.stats.max_current_frontier
                , state.stats.max_next_frontier
                , state.stats.rejected_suffix_lower_bound
                , state.stats.suffix_lower_bound_rejections.exact_dominance
                , state.stats.suffix_lower_bound_rejections.tolerance_impedance
                , state.stats.suffix_lower_bound_rejections.tolerance_journey_time
                , state.stats.suffix_lower_bound_rejections.tolerance_transfers
                , state.stats.stale_frontier_skipped
                , state.stats.c_y_removed_dominated
                , state.stats.c_y_removed_stale
                , state.stats.frontier_compaction_runs
                , state.stats.frontier_compaction_removed
                , state.stats.frontier_compaction_removed_current
                , state.stats.frontier_compaction_removed_next
                , state.stats.post_layer_day_path_candidates
                , state.stats.post_layer_day_path_inserted
                , state.stats.post_layer_day_path_representative_replaced
                , state.stats.post_layer_day_path_supports
                , state.stats.paper_timed_lookup_skipped_phase
                , state.stats.paper_timed_lookup_skipped_transfer_budget
                , state.stats.paper_timed_successor_rejected_time_domain
                , state.stats.paper_timed_successor_rejected_same_trip
                , state.stats.paper_timed_successor_rejected_same_line
                , state.stats.paper_timed_successor_rejected_feasibility
                , state.stats.rejected_time_domain
                , state.stats.rejected_feasibility
                , state.stats.rejected_reboarding
                , format_walk_lookup_stats(state.stats.walk_lookup)
                , format_walk_kind_stats(state.stats.generated_walk)
                , format_walk_kind_stats(state.stats.accepted_walk)
                , state.stats.rejected_consecutive_walk
                , format_branch_phase_stats(state.stats.accepted_branches_by_phase)
            )
            , timetable::infra::LogLevel::Info
        );
        timetable::infra::progress::log(
              format_search_pruning_runtime_stats(summarize(state.stats.pruning))
            , timetable::infra::LogLevel::Info
        );
        emit_storage_diagnostics();
    }

}  // namespace timetable::domain::assignment::runtime
