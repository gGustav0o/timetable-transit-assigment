#include "timetable/domain/assignment/search/runtime/od_day_frontier_synchronization.hpp"

#include <utility>
#include <vector>

namespace timetable::domain::assignment::runtime {
    namespace {

        void release_od_day_branch_if_closed(
              BranchArena& branches
            , std::size_t  branch_index
            , std::size_t& released_branches
        ) {
            release_branch_if_closed(
                  branches
                , branch_index
                , [&](std::size_t) noexcept {
                      ++released_branches;
                  }
            );
        }

        [[nodiscard]] std::size_t compact_frontier_queue(
              SearchFrontierLayer&                    queue
            , BranchPhaseStats&                       phase_stats
            , BranchArena&                            branches
            , const PaperConnectionLabelRegistry&     label_registry
            , std::size_t&                            released_branches
        ) {
            auto kept = std::vector<std::size_t>{};
            kept.reserve(queue.size());
            auto removed = std::size_t{ 0u };
            for (auto i = queue.head; i < queue.entries.size(); ++i) {
                const auto queued_index = queue.entries[i];
                if (!branch_alive(branches, queued_index)) {
                    ++removed;
                    continue;
                }
                const auto& queued_branch = branch_at(branches, queued_index);
                if (paper_connection_label_active(
                      label_registry
                    , queued_branch.paper_connection_label
                )) {
                    kept.push_back(queued_index);
                    continue;
                }
                decrement_phase_stats(phase_stats, queued_branch.trace.phase);
                release_od_day_branch_if_closed(
                      branches
                    , queued_index
                    , released_branches
                );
                ++removed;
            }
            queue.entries = std::move(kept);
            queue.head = 0u;
            return removed;
        }

    }  // namespace

    bool synchronize_od_day_frontier_branch(
          BranchArena&                              branches
        , std::size_t                               branch_index
        , const PaperConnectionLabelRegistry&       label_registry
        , std::size_t&                              released_branches
    ) {
        const auto& branch = branch_at(branches, branch_index);
        if (paper_connection_label_active(
              label_registry
            , branch.paper_connection_label
        )) {
            return true;
        }
        release_od_day_branch_if_closed(
              branches
            , branch_index
            , released_branches
        );
        return false;
    }

    OdDayFrontierCompactionResult compact_od_day_frontiers(
          SearchLevelExpansion&                    expansion
        , BranchArena&                             branches
        , const PaperConnectionLabelRegistry&      label_registry
        , std::size_t&                             released_branches
    ) {
        return OdDayFrontierCompactionResult{
              .removed_current = compact_frontier_queue(
                    expansion.current_frontier
                  , expansion.current_frontier_by_phase
                  , branches
                  , label_registry
                  , released_branches
                )
            , .removed_next = compact_frontier_queue(
                    expansion.next_frontier
                  , expansion.next_frontier_by_phase
                  , branches
                  , label_registry
                  , released_branches
                )
        };
    }

}  // namespace timetable::domain::assignment::runtime
