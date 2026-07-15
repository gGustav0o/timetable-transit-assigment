#include "timetable/domain/assignment/search/runtime/projection_application.hpp"

#include <algorithm>
#include <vector>

#include "timetable/domain/assignment/search/projection/complete_connection.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        [[nodiscard]] std::vector<std::size_t> matching_complete_tasks(
              const SearchBranch&                   branch
            , const ActiveIndexSet&                  active_tasks
            , std::span<const SearchProjectionSlot>  batch_tasks
        ) {
            std::vector<std::size_t> matches;
            if (!branch.metrics.departure.has_value()
                || branch.trace.current_physical.kind != EndpointKind::Zone) {
                return matches;
            }

            active_tasks.for_each_index([&](std::size_t task_pos) {
                if (batch_tasks[task_pos].destination.get()
                    == branch.trace.current_physical.id) {
                    matches.push_back(task_pos);
                }
            });
            return matches;
        }

        [[nodiscard]] bool matches_completion_target(
              const SearchBranch&                    branch
            , const ActiveIndexSet&                   active_targets
            , std::span<const SearchCompletionTarget> batch_targets
        ) noexcept {
            if (!branch.metrics.departure.has_value()
                || branch.trace.current_physical.kind != EndpointKind::Zone) {
                return false;
            }

            bool matches = false;
            active_targets.for_each_index([&](std::size_t target_pos) {
                if (batch_targets[target_pos].destination.get()
                    == branch.trace.current_physical.id) {
                    matches = true;
                }
            });
            return matches;
        }

        void add_complete_projection_retention_diagnostics(
              TaskSearchStats&                             stats
            , const CompleteProjectionRetentionDiagnostics& diagnostics
        ) noexcept {
            stats.completed_connections += diagnostics.completed_connections;
            stats.rejected_complete_admissibility +=
                diagnostics.rejected_complete_admissibility;
            stats.rejected_complete_dominance +=
                diagnostics.rejected_complete_dominance;
            stats.removed_complete_dominated +=
                diagnostics.removed_complete_dominated;
            stats.post_layer_day_path_candidates +=
                diagnostics.post_layer_day_path_candidates;
            stats.post_layer_day_path_inserted +=
                diagnostics.post_layer_day_path_inserted;
            stats.post_layer_day_path_representative_replaced +=
                diagnostics.post_layer_day_path_representative_replaced;
            stats.post_layer_day_path_supports = std::max(
                  stats.post_layer_day_path_supports
                , diagnostics.post_layer_day_path_supports
            );
        }

    }  // namespace

    mathfp::Expected<ProjectionApplicationResult> apply_projection_sink(
          SearchBatchContext&    context
        , const SearchBranch&    candidate
        , const SearchSuccessor& successor
        , const ActiveIndexSet&  active_tasks
        , const ActiveIndexSet&  active_targets
    ) {
        const auto& fixed = context.fixed;
        auto& state = context.mutable_state;

        std::vector<std::size_t> complete_task_positions;
        bool completed_target = false;
        if (fixed.od_day_slots
            && candidate.metrics.departure.has_value()
            && candidate.trace.current_physical.kind == EndpointKind::Zone) {
            const auto position_it =
                fixed.projection_positions_by_destination->find(
                    ZoneId{ candidate.trace.current_physical.id }
                );
            if (position_it != fixed.projection_positions_by_destination->end()) {
                completed_target = true;
                complete_task_positions.push_back(position_it->second);
            }
        } else if (fixed.target_projection_slots
            && candidate.metrics.departure.has_value()
            && candidate.trace.current_physical.kind == EndpointKind::Zone) {
            const auto position_it =
                fixed.target_positions_by_destination.find(
                    ZoneId{ candidate.trace.current_physical.id }
                );
            if (position_it != fixed.target_positions_by_destination.end()
                && active_targets.contains(position_it->second)) {
                completed_target = true;
                complete_task_positions.push_back(position_it->second);
            }
        } else {
            completed_target = matches_completion_target(
                  candidate
                , active_targets
                , fixed.batch_targets
            );
            if (completed_target) {
                complete_task_positions = matching_complete_tasks(
                      candidate
                    , active_tasks
                    , fixed.batch_tasks
                );
            }
        }

        if (completed_target) {
            bool retained_complete = false;
            for (const auto task_pos : complete_task_positions) {
                auto complete_result = retain_complete_projection_for_slot(
                      candidate
                    , state.branches
                    , fixed.network
                    , fixed.params.transfers
                    , fixed.search_cost
                    , fixed.batch.projection_slots[task_pos]
                    , fixed.assignment_period
                    , fixed.admissibility_config
                    , fixed.complete_connection_dominance
                    , state.retentions[task_pos]
                );
                if (!complete_result) {
                    return mathfp::unexpected(std::move(complete_result.error()));
                }
                add_complete_projection_retention_diagnostics(
                      state.task_stats[task_pos]
                    , *complete_result
                );
                add_complete_projection_retention_diagnostics(
                      state.stats
                    , *complete_result
                );
                retained_complete =
                    retained_complete
                    || complete_result->completed_connections > 0u;
            }
            if (retained_complete && successor.walk_transition.has_value()) {
                increment_walk_kind_stats(
                      state.stats.accepted_walk
                    , successor.walk_transition->kind
                );
            }
            return ProjectionApplicationResult{
                .outcome = ProjectionApplicationOutcome::CompletedProjection
            };
        }

        if (candidate.trace.current_physical.kind == EndpointKind::Zone) {
            ++state.stats.rejected_dominance_or_tolerance;
            return ProjectionApplicationResult{
                .outcome = ProjectionApplicationOutcome::RejectedZoneSink
            };
        }

        return ProjectionApplicationResult{
            .outcome = ProjectionApplicationOutcome::ContinueSearch
        };
    }

}  // namespace timetable::domain::assignment::runtime
