#include "timetable/domain/assignment/search/runtime/branch_enqueue.hpp"

#include <utility>

#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/assignment/search/tree/level_expansion.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        void record_accepted_branch_stats(
              TaskSearchStats&       stats
            , const SearchBranch&     branch
            , const SearchSuccessor&  successor
        ) noexcept {
            ++stats.accepted_branches;
            if (successor.walk_transition.has_value()) {
                increment_walk_kind_stats(
                      stats.accepted_walk
                    , successor.walk_transition->kind
                );
            }
            increment_phase_stats(
                  stats.accepted_branches_by_phase
                , branch.trace.phase
            );
        }

    }  // namespace

    std::size_t enqueue_od_day_branch(
          SearchBatchContext&    context
        , SearchBranch           candidate
        , const SearchSuccessor& successor
    ) {
        auto& state = context.mutable_state;

        record_accepted_branch_stats(state.stats, candidate, successor);
        const auto candidate_phase = candidate.trace.phase;
        const auto candidate_index = append_branch(
              state.branches
            , std::move(candidate)
        );
        push_search_level_branch(
              state.level_expansion
            , candidate_index
            , candidate_phase
            , SearchLevelPlacement::Next
        );
        return candidate_index;
    }

    std::size_t enqueue_projected_branch(
          SearchBatchContext&              context
        , const SearchBranch&              parent_branch
        , const ConnectionSegment&         connection
        , SearchBranch                     candidate
        , const SearchSuccessor&           successor
        , SearchContinuationProjection     projection
    ) {
        const auto& fixed = context.fixed;
        auto& state = context.mutable_state;

        record_accepted_branch_stats(state.stats, candidate, successor);
        const auto level_placement =
            paper_successor_level_placement(parent_branch, connection);
        const auto candidate_phase = candidate.trace.phase;
        const auto candidate_index = append_branch(
              state.branches
            , std::move(candidate)
        );
        if (fixed.target_projection_slots) {
            state.completion_projection_states.push_back(
                FixedActiveMask::from(projection.active_tasks)
            );
        } else {
            state.demand_projection_states.push_back(
                DemandBranchProjectionState{
                      .active_tasks = std::move(projection.active_tasks)
                    , .active_targets = std::move(projection.active_targets)
                }
            );
        }
        push_search_level_branch(
              state.level_expansion
            , candidate_index
            , candidate_phase
            , level_placement
        );
        return candidate_index;
    }

}  // namespace timetable::domain::assignment::runtime
