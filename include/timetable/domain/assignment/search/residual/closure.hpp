#pragma once

#include "timetable/domain/assignment/search/residual/graph.hpp"
#include "timetable/domain/assignment/search/residual/types.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] ResidualReachabilityStateSet build_residual_reachable_states(
          const ResidualReverseGraph& graph
        , ZoneId                      destination
        , TransferCount               max_transfers
    );

}  // namespace timetable::domain::assignment
