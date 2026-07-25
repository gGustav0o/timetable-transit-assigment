#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/residual/types.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_destination_residual_reachability(
          ZoneId                                destination
        , const DestinationResidualReachability& reachability
        , TransferCount                         max_transfers
    );

    mathfp::Expected<mathfp::Unit> validate_residual_reachability(
          const ResidualReachability& reachability
        , TransferCount               max_transfers
    );

}  // namespace timetable::domain::assignment
