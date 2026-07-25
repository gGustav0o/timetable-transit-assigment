#pragma once

#include "timetable/domain/assignment/search/residual/types.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] ReachabilityDecision evaluate_residual_reachability(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept;

    [[nodiscard]] bool can_have_feasible_suffix(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept;

}  // namespace timetable::domain::assignment
