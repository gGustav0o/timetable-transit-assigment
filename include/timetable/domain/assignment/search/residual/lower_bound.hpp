#pragma once

#include "timetable/domain/assignment/search/residual/graph.hpp"
#include "timetable/domain/assignment/search/residual/types.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] ResidualSuffixLowerBoundMap build_residual_suffix_lower_bounds(
          const ResidualReverseGraph& graph
        , ZoneId                      destination
        , TransferCount               max_transfers
        , const SearchImpedance&      impedance
        , double                      fare_scale
    );

}  // namespace timetable::domain::assignment
