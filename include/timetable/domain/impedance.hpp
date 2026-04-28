#pragma once

#include <mathfp/types/units.hpp>

#include "timetable/domain/params.hpp"

namespace timetable::domain {
    struct ConnectionImpedanceComponents final {
        Time          in_vehicle_time{};
        Time          access_time{};
        Time          egress_time{};
        Time          transfer_walk_time{};
        Time          transfer_wait_time{};
        TransferCount transfer_count{};
        double        fare{};
    };

    inline double normalized_fare_value(
          double   fare
        , double fare_scale
    ) noexcept {
        if (fare_scale <= 0.0) {
            return 0.0;
        }
        return fare / fare_scale;
    }

    inline double weighted_duration(
          Time    duration
        , Dimless weight
    ) noexcept {
        return mathfp::units::as_dimless(weight) * duration.value();
    }

    inline double weighted_transfer_count(
          TransferCount transfers
        , Dimless       weight
    ) noexcept {
        return mathfp::units::as_dimless(weight) * static_cast<double>(transfers.get());
    }

    inline double connection_impedance_value(
          const ConnectionImpedanceComponents& components
        , const SearchImpedance& weights
        , double                 fare_scale
    ) noexcept {
        return
              weighted_duration      (components.in_vehicle_time   , weights.in_vehicle_time)
            + weighted_duration      (components.access_time       , weights.access_time)
            + weighted_duration      (components.egress_time       , weights.egress_time)
            + weighted_duration      (components.transfer_walk_time, weights.transfer_walk_time)
            + weighted_duration      (components.transfer_wait_time, weights.transfer_wait_time)
            + weighted_transfer_count(components.transfer_count    , weights.transfer_count)
            + mathfp::units::as_dimless(weights.fare)
                * normalized_fare_value(components.fare, fare_scale);
    }

}  // namespace timetable::domain
