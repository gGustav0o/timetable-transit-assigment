#pragma once

#include <span>

#include "timetable/domain/assignment/search/problem.hpp"
#include "timetable/domain/assignment/search/residual/graph.hpp"
#include "timetable/domain/assignment/search/residual/query.hpp"
#include "timetable/domain/assignment/search/residual/types.hpp"
#include "timetable/domain/assignment/search/residual/validation.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] ResidualReachability build_residual_reachability(
          const ResidualReverseGraph& graph
        , std::span<const SearchCompletionTarget> targets
        , TransferCount               max_transfers
        , const SearchImpedance&      impedance
        , double                      fare_scale
    );

}  // namespace timetable::domain::assignment
