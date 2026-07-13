#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"

namespace timetable::domain::assignment {

    Time partial_journey_time(
        const SearchPartialMetrics& metrics
    ) noexcept {
        return Time{
            metrics.current_time->value() - metrics.departure->value()
        };
    }

    Time partial_walk_time(
        const SearchPartialMetrics& metrics
    ) noexcept {
        return metrics.access_time + metrics.transfer_walk_time + metrics.egress_time;
    }

    ConnectionImpedanceComponents partial_impedance_components(
        const SearchPartialMetrics& metrics
    ) noexcept {
        return ConnectionImpedanceComponents{
              .in_vehicle_time    = metrics.in_vehicle_time
            , .access_time        = metrics.access_time
            , .egress_time        = metrics.egress_time
            , .transfer_walk_time = metrics.transfer_walk_time
            , .transfer_wait_time = metrics.transfer_wait_time
            , .transfer_count     = metrics.transfers
            , .fare               = metrics.fare
        };
    }

}  // namespace timetable::domain::assignment
