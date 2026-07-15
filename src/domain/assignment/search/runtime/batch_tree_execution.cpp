#include "timetable/domain/assignment/search/runtime/batch_tree_execution.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/od_day/successor.hpp"
#include "timetable/domain/assignment/search/runtime/accepted_successor_application.hpp"
#include "timetable/domain/assignment/search/runtime/cancellation.hpp"
#include "timetable/domain/assignment/search/runtime/od_day_frontier_synchronization.hpp"
#include "timetable/domain/assignment/search/tree/level_expansion.hpp"
#include "timetable/domain/assignment/search/tree/paper_successor_step.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        constexpr std::size_t kSearchHeartbeatStep = 100'000;
        constexpr std::size_t kSearchWallClockSuccessorCheckStep = 16'384;
        constexpr std::size_t kOdDayMemoryLimitCheckStep = 1'024;
        constexpr auto kOdDayFrontierCompactionMinRemoved =
            std::size_t{ 1024u };
        constexpr auto kOdDayFrontierCompactionMinSize =
            std::size_t{ 4096u };

        void add_paper_successor_generation_diagnostics(
              TaskSearchStats&                          stats
            , const PaperSuccessorGenerationDiagnostics& diagnostics
        ) noexcept {
            stats.walk_lookup.access += diagnostics.walk_lookup.access;
            stats.walk_lookup.transfer += diagnostics.walk_lookup.transfer;
            stats.walk_lookup.egress += diagnostics.walk_lookup.egress;
            stats.walk_lookup.skipped_by_phase += diagnostics.walk_lookup.skipped_by_phase;
            stats.walk_lookup.skipped_by_transfer_budget +=
                diagnostics.walk_lookup.skipped_by_transfer_budget;
            stats.paper_timed_lookup_skipped_phase +=
                diagnostics.timed_lookup_skipped_phase;
            stats.paper_timed_lookup_skipped_transfer_budget +=
                diagnostics.timed_lookup_skipped_transfer_budget;
            stats.paper_timed_successor_rejected_time_domain +=
                diagnostics.timed_successor_rejected_time_domain;
            stats.paper_timed_successor_rejected_same_trip +=
                diagnostics.timed_successor_rejected_same_trip;
            stats.paper_timed_successor_rejected_same_line +=
                diagnostics.timed_successor_rejected_same_line;
            stats.paper_timed_successor_rejected_feasibility +=
                diagnostics.timed_successor_rejected_feasibility;
        }

        void record_paper_successor_feasibility_rejection(
              TaskSearchStats&                   stats
            , PaperSuccessorFeasibilityRejection rejection
        ) noexcept {
            switch (rejection) {
                case PaperSuccessorFeasibilityRejection::None:
                    return;

                case PaperSuccessorFeasibilityRejection::FirstDepartureDomain:
                    ++stats.rejected_time_domain;
                    return;

                case PaperSuccessorFeasibilityRejection::BranchFeasibility:
                    ++stats.rejected_feasibility;
                    return;

                case PaperSuccessorFeasibilityRejection::Reboarding:
                    ++stats.rejected_reboarding;
                    return;
            }
        }

        template <typename Visitor, typename RejectedWalkVisitor>
        void for_each_successor(
              const PreprocessedNetwork& network
            , const DayLevelSupplySearchGraph* day_graph
            , ZoneId                      origin
            , const ActiveDestinationMembership& active_destinations
            , const SearchBranch&        branch
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
            , TaskSearchStats*           stats
            , Visitor&&                  visit
            , RejectedWalkVisitor&&      reject_walk
        ) {
            auto&& visitor = visit;
            auto&& walk_rejection_visitor = reject_walk;
            if (day_graph != nullptr) {
                for_each_day_level_supply_successor(
                      *day_graph
                    , network
                    , origin
                    , active_destinations
                    , branch
                    , limits
                    , first_departure_domain
                    , visitor
                    , walk_rejection_visitor
                );
                return;
            }

            auto diagnostics = PaperSuccessorGenerationDiagnostics{};
            for_each_paper_successor(
                  network
                , origin
                , active_destinations
                , branch
                , limits
                , first_departure_domain
                , stats != nullptr ? &diagnostics : nullptr
                , visitor
            );
            if (stats != nullptr) {
                add_paper_successor_generation_diagnostics(*stats, diagnostics);
            }
            (void)walk_rejection_visitor;
        }

    }  // namespace

    mathfp::Expected<SearchTreeRunStatus> run_search_batch_tree(
          SearchBatchContext&             context
        , SearchBatchDiagnosticsRuntime&  diagnostics
        , const OdDayProductionMemoryLimits& od_day_memory_limits
    ) {
        const auto& fixed = context.fixed;
        auto& state = context.mutable_state;
        auto c_y_removed_since_frontier_compaction = std::size_t{ 0u };

        auto release_projection_payload = [&](std::size_t released_index) noexcept {
            if (fixed.od_day_slots) {
                ++state.released_branches;
                return;
            }
            if (fixed.target_projection_slots) {
                if (released_index < state.completion_projection_states.size()) {
                    state.completion_projection_states[released_index].reset();
                }
            } else if (released_index < state.demand_projection_states.size()) {
                state.demand_projection_states[released_index].reset();
            }
            ++state.released_branches;
        };

        auto compact_od_day_frontier_state = [&](const char* reason) {
            if (!fixed.od_day_slots) {
                return;
            }
            const auto compaction = compact_od_day_frontiers(
                  state.level_expansion
                , state.branches
                , state.paper_label_registry
                , state.released_branches
            );
            const auto removed = compaction.removed();
            if (removed == 0u) {
                return;
            }
            ++state.stats.frontier_compaction_runs;
            state.stats.frontier_compaction_removed += removed;
            state.stats.frontier_compaction_removed_current +=
                compaction.removed_current;
            state.stats.frontier_compaction_removed_next +=
                compaction.removed_next;
            diagnostics.emit_frontier_compacted(reason, compaction);
        };

        const auto paper_connection_tree_targets =
            fixed.od_day_slots
                ? ActiveIndexSet::full(fixed.batch.completion_targets.size())
                : ActiveIndexSet{};

        return run_search_tree_levels(
              state.level_expansion
            , [&]() -> mathfp::Expected<SearchTreeRunDirective> {
                  if (search_cancelled(fixed.cancellation)) {
                      diagnostics.emit_batch_cancelled_before_tree();
                      return SearchTreeRunDirective::Stop;
                  }
                  return SearchTreeRunDirective::Continue;
              }
            , [&]() -> mathfp::Expected<mathfp::Unit> {
                  compact_od_day_frontier_state("level_swap");
                  c_y_removed_since_frontier_compaction = 0u;
                  return mathfp::kUnit;
              }
            , [&](std::size_t queued_branch_index) noexcept {
                  return branch_at(
                      state.branches
                    , queued_branch_index
                  ).trace.phase;
              }
            , [&](std::size_t branch_index)
                  -> mathfp::Expected<SearchTreeRunDirective> {
                  auto& stats = state.stats;
                  auto& expansion = state.level_expansion;
                  stats.max_current_frontier = std::max(
                        stats.max_current_frontier
                      , expansion.current_frontier.size()
                  );
                  stats.max_next_frontier = std::max(
                        stats.max_next_frontier
                      , expansion.next_frontier.size()
                  );

                  if (fixed.od_day_slots
                      && !synchronize_od_day_frontier_branch(
                            state.branches
                          , branch_index
                          , state.paper_label_registry
                          , state.released_branches
                      )) {
                      ++stats.stale_frontier_skipped;
                      return SearchTreeRunDirective::Continue;
                  }

                  const auto& branch = branch_at(state.branches, branch_index);
                  diagnostics.emit_wall_clock_heartbeat("branch", branch_index);

                  ActiveIndexSet completion_active;
                  const ActiveIndexSet* active_tasks_ptr{};
                  const ActiveIndexSet* active_targets_ptr{};
                  if (fixed.od_day_slots) {
                      active_tasks_ptr = &paper_connection_tree_targets;
                      active_targets_ptr = &paper_connection_tree_targets;
                  } else if (fixed.target_projection_slots) {
                      completion_active =
                          state.completion_projection_states[branch_index]
                              ->to_active_index_set();
                      active_tasks_ptr = &completion_active;
                      active_targets_ptr = &completion_active;
                  } else {
                      const auto& projection_state =
                          *state.demand_projection_states[branch_index];
                      active_tasks_ptr = &projection_state.active_tasks;
                      active_targets_ptr = &projection_state.active_targets;
                  }

                  const auto& active_tasks = *active_tasks_ptr;
                  const auto& active_targets = *active_targets_ptr;
                  const auto active_destinations = fixed.od_day_slots
                      ? ActiveDestinationMembership{
                            .direct_destination_ids =
                                fixed.od_day_destination_ids
                        }
                      : ActiveDestinationMembership{
                            .active_targets = &active_targets
                          , .batch_targets = fixed.batch_targets
                        };
                  ++stats.expanded_branches;

                  if (fixed.od_day_slots
                      && ((stats.expanded_branches
                              % kOdDayMemoryLimitCheckStep) == 0u)) {
                      MATHFP_TRY(validate_od_day_production_memory_limits(
                            diagnostics.storage_diagnostics()
                          , expansion.current_frontier.size()
                          , expansion.next_frontier.size()
                          , od_day_memory_limits
                          , fixed.batch.key.origin
                      ));
                  }

                  if ((stats.expanded_branches % kSearchHeartbeatStep) == 0u) {
                      diagnostics.emit_search_heartbeat();
                  }

                  if (active_targets.empty()
                      || (branch.trace.current_physical.kind == EndpointKind::Zone
                          && branch.metrics.departure.has_value())) {
                      release_branch_if_closed(
                            state.branches
                          , branch_index
                          , release_projection_payload
                      );
                      return SearchTreeRunDirective::Continue;
                  }

                  mathfp::Expected<mathfp::Unit> successor_error = mathfp::kUnit;
                  bool batch_cancelled = false;
                  for_each_successor(
                        fixed.network
                      , fixed.od_day_supply
                      , fixed.batch.key.origin
                      , active_destinations
                      , branch
                      , fixed.params.transfers
                      , fixed.first_departure_domain
                      , &stats
                      , [&](const SearchSuccessor& successor_ref) {
                            if (search_cancelled(fixed.cancellation)) {
                                batch_cancelled = true;
                                return;
                            }
                            if (!successor_error) {
                                return;
                            }
                            ++stats.generated_successors;
                            if (successor_ref.walk_transition.has_value()) {
                                increment_walk_kind_stats(
                                      stats.generated_walk
                                    , successor_ref.walk_transition->kind
                                );
                            }
                            if ((stats.generated_successors
                                    % kSearchWallClockSuccessorCheckStep)
                                == 0u) {
                                diagnostics.emit_wall_clock_heartbeat(
                                      "successor"
                                    , branch_index
                                );
                            }

                            const auto& successor = connection_segment_at(
                                  fixed.network
                                , successor_ref.connection
                            );
                            auto step_decision = evaluate_paper_successor_step(
                                  state.branches
                                , branch_index
                                , branch
                                , fixed.network
                                , successor_ref
                                , fixed.batch.key.interval
                                , fixed.first_departure_domain
                                , fixed.params.transfers
                                , fixed.search_cost
                                , PaperSuccessorStepConfig{
                                      .late_feasibility_prechecked =
                                          fixed.od_day_slots
                                          && successor_ref.support_envelope.has_value()
                                    , .retain_in_paper_c_y =
                                          fixed.od_day_slots
                                          && fixed.partial_retention_scope
                                              == SearchPartialRetentionScope::TreeGlobal
                                  }
                                , state.paper_label_registry
                                , state.tree_partial_retention.paper_connections
                                , fixed.pruning_execution
                                , stats.pruning
                            );
                            if (!step_decision) {
                                successor_error = mathfp::unexpected(
                                    std::move(step_decision.error())
                                );
                                return;
                            }
                            if (step_decision->retention.has_value()
                                && step_decision->retention->accepted()) {
                                const auto& retention = *step_decision->retention;
                                for (const auto removed_label :
                                     retention.removed_labels) {
                                    deactivate_paper_connection_label(
                                          state.paper_label_registry
                                        , removed_label
                                    );
                                }
                                const auto removed_from_c_y =
                                      retention.removed_labels.size()
                                    + retention.removed_stale_labels;
                                stats.c_y_removed_dominated +=
                                    retention.removed_labels.size();
                                stats.c_y_removed_stale +=
                                    retention.removed_stale_labels;
                                c_y_removed_since_frontier_compaction +=
                                    removed_from_c_y;
                                if (c_y_removed_since_frontier_compaction
                                        >= kOdDayFrontierCompactionMinRemoved
                                    && (expansion.current_frontier.size()
                                        + expansion.next_frontier.size())
                                        >= kOdDayFrontierCompactionMinSize) {
                                    compact_od_day_frontier_state("c_y_removal");
                                    c_y_removed_since_frontier_compaction = 0u;
                                }
                            }
                            if (!step_decision->accepted()) {
                                switch (step_decision->rejection) {
                                    case PaperSuccessorStepRejection::None:
                                        return;

                                    case PaperSuccessorStepRejection::LateFeasibility:
                                        record_paper_successor_feasibility_rejection(
                                              stats
                                            , step_decision->feasibility_rejection
                                        );
                                        if (fixed.od_day_slots) {
                                            successor_error = mathfp::unexpected(
                                                mathfp::internal_error("OD-day paper successor failed late insertability invariant")
                                                    .ctx("origin", fixed.batch.key.origin.get())
                                                    .ctx("branch", static_cast<std::int64_t>(branch_index))
                                                    .ctx("connection", static_cast<std::int64_t>(successor_ref.connection.get()))
                                                    .ctx("rejection", static_cast<std::int64_t>(step_decision->feasibility_rejection))
                                            );
                                        }
                                        return;

                                    case PaperSuccessorStepRejection::PrefixCycle:
                                    case PaperSuccessorStepRejection::BranchCycle:
                                        ++stats.rejected_cycles;
                                        return;

                                    case PaperSuccessorStepRejection::PrefixRejected:
                                        return;

                                    case PaperSuccessorStepRejection::PrefixTransferLimit:
                                    case PaperSuccessorStepRejection::BranchTransferLimit:
                                        ++stats.rejected_transfer_limit;
                                        return;

                                    case PaperSuccessorStepRejection::PrefixRetention:
                                        ++stats.rejected_dominance_or_tolerance;
                                        return;
                                }
                            }

                            auto candidate = std::move(*step_decision->branch);
                            if (fixed.od_day_slots) {
                                candidate.paper_connection_label =
                                    step_decision->accepted_paper_label;
                            }
                            auto application_result = apply_accepted_successor(
                                  context
                                , branch
                                , successor_ref
                                , successor
                                , std::move(candidate)
                                , active_tasks
                                , active_targets
                            );
                            if (!application_result) {
                                successor_error = mathfp::unexpected(
                                    std::move(application_result.error())
                                );
                                return;
                            }
                            if (application_result->outcome
                                    == AcceptedSuccessorApplicationOutcome::Enqueued
                                && fixed.od_day_slots
                                && od_day_memory_limits.max_frontier_per_tree.has_value()
                                && (expansion.current_frontier.size()
                                    + expansion.next_frontier.size()
                                    > *od_day_memory_limits.max_frontier_per_tree)) {
                                successor_error =
                                    validate_od_day_production_memory_limits(
                                          diagnostics.storage_diagnostics()
                                        , expansion.current_frontier.size()
                                        , expansion.next_frontier.size()
                                        , od_day_memory_limits
                                        , fixed.batch.key.origin
                                    );
                            }
                        }
                      , [&](std::size_t rejected_walk_count) {
                            stats.rejected_consecutive_walk += rejected_walk_count;
                        }
                  );
                  release_branch_if_closed(
                        state.branches
                      , branch_index
                      , release_projection_payload
                  );
                  if (batch_cancelled) {
                      diagnostics.emit_batch_cancelled_during_successor_scan();
                      return SearchTreeRunDirective::Stop;
                  }
                  MATHFP_TRY(std::move(successor_error));
                  return SearchTreeRunDirective::Continue;
              }
        );
    }

}  // namespace timetable::domain::assignment::runtime
