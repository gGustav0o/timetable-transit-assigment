#pragma once

#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] bool paper_node_connection_relevant(
          const ConnectionSetCy&        set
        , const ExactPruningPolicy&     policy
        , const SearchPruningMetrics&   candidate
    ) noexcept;

    [[nodiscard]] SearchPruningDecision evaluate_paper_node_connection_set(
          const SearchPruningExecutionPlan& execution
        , const SearchPruningMetrics&       candidate
        , const ConnectionSetCy&            set
        , const TransferLimits&             limits
    ) noexcept;

}  // namespace timetable::domain::assignment
