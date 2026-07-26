#pragma once

#include <cstddef>

#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/tree/level_expansion.hpp"

namespace timetable::domain::assignment::runtime {

    struct OdDayFrontierCompactionResult final {
        std::size_t removed_current{};
        std::size_t removed_next{};

        [[nodiscard]] std::size_t removed() const noexcept {
            return removed_current + removed_next;
        }
    };

    [[nodiscard]] bool synchronize_od_day_frontier_branch(
          BranchArena&                              branches
        , std::size_t                               branch_index
        , const RetainedConnectionLabelRegistry&       label_registry
        , std::size_t&                              released_branches
    );

    [[nodiscard]] OdDayFrontierCompactionResult compact_od_day_frontiers(
          SearchLevelExpansion&                    expansion
        , BranchArena&                             branches
        , const RetainedConnectionLabelRegistry&      label_registry
        , std::size_t&                             released_branches
    );

}  // namespace timetable::domain::assignment::runtime
