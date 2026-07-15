#include "timetable/domain/assignment/search/runtime/continuation_filter.hpp"

#include <span>
#include <utility>
#include <vector>

#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/pruning/suffix_lower_bound.hpp"

namespace timetable::domain::assignment::runtime {

    mathfp::Expected<std::optional<SearchContinuationProjection>>
    filter_continuation_projection(
          SearchBatchContext&   context
        , const SearchBranch&   candidate
        , const ActiveIndexSet& active_tasks
        , const ActiveIndexSet& active_targets
    ) {
        const auto& fixed = context.fixed;
        auto& state = context.mutable_state;

        const auto candidate_reachability_key = reachability_mask_key(
              candidate
            , fixed.params.transfers
        );
        const auto& target_reachability_entry =
            state.reachability_cache->target_entry(candidate_reachability_key);
        auto next_active_targets = filter_target_positions_by_reachability(
              active_targets
            , target_reachability_entry
        );
        if (next_active_targets.empty()) {
            add_reachability_rejection(
                  state.stats
                , summarize_target_reachability_rejection(
                      active_targets
                    , target_reachability_entry
                  )
            );
            return std::nullopt;
        }

        if (fixed.partial_retention_scope == SearchPartialRetentionScope::TreeGlobal) {
            auto pruning_decision = retain_branch(
                  candidate
                , state.tree_partial_retention
                , fixed.params
                , fixed.search_cost
                , fixed.pruning_execution
                , state.stats.pruning
            );
            if (!pruning_decision) {
                return mathfp::unexpected(std::move(pruning_decision.error()));
            }
            if (!pruning_decision->accepted) {
                ++state.stats.rejected_dominance_or_tolerance;
                return std::nullopt;
            }
        }

        const auto reachable_tasks = filter_task_positions_by_reachability(
              active_tasks
            , state.reachability_cache->slot_entry(candidate_reachability_key)
        );
        record_reachability_rejections(
              std::span<const RejectedReachabilityTask>{
                  reachable_tasks.unreachable.data()
                , reachable_tasks.unreachable.size()
              }
            , state.task_stats
            , state.stats
        );

        ActiveIndexSet next_active_tasks{ fixed.batch.projection_slots.size() };
        std::vector<RejectedSuffixLowerBoundTask> lower_bound_rejected_tasks;
        lower_bound_rejected_tasks.reserve(
            reachable_tasks.reachable.active_count()
        );
        auto successor_error = mathfp::Expected<mathfp::Unit>{ mathfp::kUnit };
        reachable_tasks.reachable.for_each_index([&](std::size_t task_pos) {
            if (!successor_error) {
                return;
            }
            if (fixed.batch.projection_slots[task_pos].kind
                == SearchProjectionSlotKind::OdDayPair) {
                next_active_tasks.set(task_pos);
                return;
            }
            auto lower_bound_decision = fixed.target_projection_slots
                ? evaluate_suffix_lower_bound_pruning(
                      candidate
                    , fixed.batch.projection_slots[task_pos].destination
                    , *state.reachability
                    , state.retentions[task_pos].compact_complete_connections
                    , fixed.params
                    , fixed.search_cost
                    , fixed.choice_config
                    , fixed.complete_connection_dominance
                  )
                : evaluate_suffix_lower_bound_pruning(
                      candidate
                    , fixed.batch.projection_slots[task_pos].destination
                    , *state.reachability
                    , state.retentions[task_pos].complete_connections
                    , fixed.params
                    , fixed.search_cost
                    , fixed.choice_config
                    , fixed.complete_connection_dominance
                  );
            if (!lower_bound_decision) {
                successor_error = mathfp::unexpected(
                    std::move(lower_bound_decision.error())
                );
                return;
            }
            if (!lower_bound_decision->feasible) {
                lower_bound_rejected_tasks.push_back(
                    RejectedSuffixLowerBoundTask{
                          .task_position = task_pos
                        , .reason = lower_bound_decision->rejection_reason
                    }
                );
                ++state.task_stats[task_pos].rejected_dominance_or_tolerance;
                return;
            }

            if (fixed.partial_retention_scope
                == SearchPartialRetentionScope::ProjectionSlotLocal) {
                auto pruning_decision = retain_branch(
                      candidate
                    , state.retentions[task_pos]
                    , fixed.params
                    , fixed.search_cost
                    , fixed.pruning_execution
                    , state.stats.pruning
                );
                if (!pruning_decision) {
                    successor_error = mathfp::unexpected(
                        std::move(pruning_decision.error())
                    );
                    return;
                }
                if (!pruning_decision->accepted) {
                    ++state.task_stats[task_pos].rejected_dominance_or_tolerance;
                    return;
                }
            }
            next_active_tasks.set(task_pos);
        });
        if (!successor_error) {
            return mathfp::unexpected(std::move(successor_error.error()));
        }
        record_suffix_lower_bound_rejections(
              std::span<const RejectedSuffixLowerBoundTask>{
                  lower_bound_rejected_tasks.data()
                , lower_bound_rejected_tasks.size()
              }
            , state.task_stats
            , state.stats
        );
        if (next_active_tasks.empty()) {
            ++state.stats.rejected_dominance_or_tolerance;
            return std::nullopt;
        }

        return SearchContinuationProjection{
              .active_tasks = std::move(next_active_tasks)
            , .active_targets = std::move(next_active_targets)
        };
    }

}  // namespace timetable::domain::assignment::runtime
