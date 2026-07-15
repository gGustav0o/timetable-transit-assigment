#include "timetable/domain/assignment/search/tree/level_expansion.hpp"

#include <utility>

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {

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

    void seed_search_level_expansion(
          SearchLevelExpansion& expansion
        , std::size_t           root_branch_index
        , SearchBranchPhase     root_phase
    ) {
        expansion.current_frontier.clear();
        expansion.next_frontier.clear();
        expansion.current_frontier_by_phase = BranchPhaseStats{};
        expansion.next_frontier_by_phase = BranchPhaseStats{};
        expansion.current_frontier.push_back(root_branch_index);
        increment_phase_stats(expansion.current_frontier_by_phase, root_phase);
    }

    void advance_search_level(
        SearchLevelExpansion& expansion
    ) noexcept {
        expansion.current_frontier.swap(expansion.next_frontier);
        expansion.current_frontier_by_phase = expansion.next_frontier_by_phase;
        expansion.next_frontier_by_phase = BranchPhaseStats{};
    }

    std::size_t pop_current_search_level_branch(
          SearchLevelExpansion& expansion
        , SearchBranchPhase     phase
    ) {
        const auto branch_index = expansion.current_frontier.front();
        expansion.current_frontier.pop_front();
        decrement_phase_stats(expansion.current_frontier_by_phase, phase);
        return branch_index;
    }

    void push_search_level_branch(
          SearchLevelExpansion& expansion
        , std::size_t           branch_index
        , SearchBranchPhase     phase
        , SearchLevelPlacement  placement
    ) {
        switch (placement) {
            case SearchLevelPlacement::Current:
                expansion.current_frontier.push_back(branch_index);
                increment_phase_stats(expansion.current_frontier_by_phase, phase);
                return;

            case SearchLevelPlacement::Next:
                expansion.next_frontier.push_back(branch_index);
                increment_phase_stats(expansion.next_frontier_by_phase, phase);
                return;
        }
    }

    SearchLevelPlacement paper_successor_level_placement(
          const SearchBranch&      parent
        , const ConnectionSegment& successor
    ) noexcept {
        return is_walk_connection(successor) || !parent.metrics.departure.has_value()
            ? SearchLevelPlacement::Current
            : SearchLevelPlacement::Next;
    }

}  // namespace timetable::domain::assignment
