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
        TransferCount transfer_count{ TransferCount{0} };
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
        //tex:
        // Search impedance is the generalized-cost form used by branch-and-bound.
        // The source formula $$IMP(c)=a_1JT(c)+a_2NT(c)+a_3FARE(c)$$ is the
        // aggregated special case. The implementation keeps $$JT(c)$$ decomposed:
        // $$IMP(c)=w_{ivt}IVT+w_{acc}ACC+w_{egr}EGR+w_{tw}TWalk+w_{twait}TWait+w_{nt}NT+w_f\,\widehat{FARE}.$$
        // Here $$\widehat{FARE}=FARE/\mathrm{fare\_scale}$$ for configured normalization.
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
