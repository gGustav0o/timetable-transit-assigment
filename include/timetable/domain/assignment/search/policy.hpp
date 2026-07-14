#pragma once

#include <utility>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Mathematical policy used while building a connection tree.
     *
     * Final connection choice is deliberately excluded. ChoiceTolerances and
     * ChoiceConfig belong to post-search pruning over complete alternatives.
     */
    struct SearchPolicy final {
        SearchCostContext                  cost{};
        TransferLimits                     transfer_limits{};
        SearchTolerances                   tolerances{};
        CompleteConnectionDominanceConfig  complete_connection_dominance{};
    };

    [[nodiscard]] inline SearchPolicy make_search_policy(
          SearchCostContext                 cost
        , const SearchParams&               params
        , CompleteConnectionDominanceConfig complete_connection_dominance = {}
    ) {
        return SearchPolicy{
              .cost                          = std::move(cost)
            , .transfer_limits               = params.transfers
            , .tolerances                    = params.search_tolerances
            , .complete_connection_dominance = complete_connection_dominance
        };
    }

}  // namespace timetable::domain::assignment
