#pragma once

#include <cstddef>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/tree/level_expansion.hpp"

namespace timetable::domain::assignment {

    enum class SearchTreeRunDirective {
          Continue
        , Stop
    };

    enum class SearchTreeRunStatus {
          Exhausted
        , Stopped
    };

    template <
          typename BeforeIteration
        , typename BeforeLevelAdvance
        , typename BranchPhaseOf
        , typename ExpandBranch
    >
    [[nodiscard]] mathfp::Expected<SearchTreeRunStatus> run_search_tree_levels(
          SearchLevelExpansion& expansion
        , BeforeIteration&&     before_iteration
        , BeforeLevelAdvance&&  before_level_advance
        , BranchPhaseOf&&       branch_phase_of
        , ExpandBranch&&        expand_branch
    ) {
        while (!expansion.empty()) {
            MATHFP_TRY_LET(
                  SearchTreeRunDirective
                , directive
                , before_iteration()
            );
            if (directive == SearchTreeRunDirective::Stop) {
                return SearchTreeRunStatus::Stopped;
            }

            if (expansion.current_frontier.empty()) {
                MATHFP_TRY(before_level_advance());
                advance_search_level(expansion);
                if (expansion.current_frontier.empty()) {
                    continue;
                }
            }

            const auto branch_index = pop_current_search_level_branch(
                  expansion
                , branch_phase_of(expansion.current_frontier.front())
            );
            MATHFP_TRY_LET(
                  SearchTreeRunDirective
                , branch_directive
                , expand_branch(branch_index)
            );
            if (branch_directive == SearchTreeRunDirective::Stop) {
                return SearchTreeRunStatus::Stopped;
            }
        }
        return SearchTreeRunStatus::Exhausted;
    }

}  // namespace timetable::domain::assignment
