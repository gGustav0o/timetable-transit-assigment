#pragma once

#include <cstddef>

#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    struct BranchPhaseStats final {
        std::size_t at_origin{};
        std::size_t before_first_boarding{};
        std::size_t after_timed_ride{};
        std::size_t after_transfer_walk{};
        std::size_t completed{};
    };

    struct SearchLevelExpansion final {
        SearchFrontierLayer current_frontier{};
        SearchFrontierLayer next_frontier{};
        BranchPhaseStats    current_frontier_by_phase{};
        BranchPhaseStats    next_frontier_by_phase{};

        [[nodiscard]] bool empty() const noexcept {
            return current_frontier.empty() && next_frontier.empty();
        }

        [[nodiscard]] std::size_t size() const noexcept {
            return current_frontier.size() + next_frontier.size();
        }
    };

    enum class SearchLevelPlacement {
          Current
        , Next
    };

    void increment_phase_stats(
          BranchPhaseStats& stats
        , SearchBranchPhase phase
    ) noexcept;

    void decrement_phase_stats(
          BranchPhaseStats& stats
        , SearchBranchPhase phase
    ) noexcept;

    void seed_search_level_expansion(
          SearchLevelExpansion& expansion
        , std::size_t           root_branch_index
        , SearchBranchPhase     root_phase
    );

    void advance_search_level(
        SearchLevelExpansion& expansion
    ) noexcept;

    [[nodiscard]] std::size_t pop_current_search_level_branch(
          SearchLevelExpansion& expansion
        , SearchBranchPhase     phase
    );

    void push_search_level_branch(
          SearchLevelExpansion& expansion
        , std::size_t           branch_index
        , SearchBranchPhase     phase
        , SearchLevelPlacement  placement
    );

    [[nodiscard]] SearchLevelPlacement paper_successor_level_placement(
          const SearchBranch&      parent
        , const ConnectionSegment& successor
    ) noexcept;

}  // namespace timetable::domain::assignment
