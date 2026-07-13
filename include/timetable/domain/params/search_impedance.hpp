#pragma once

#include <cstdint>

#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

    struct FareNormalization final {
        enum class Kind : std::uint8_t {
              None
            , Mean
            , Median
            , P95
            , FixedScale
        };

        Kind kind{ Kind::Median };
        double fixed_scale{ 1.0 };
    };

    /**
     * @brief Component search impedance weights for branch-and-bound search.
     *
     * The search cost is a generalized cost over parameter-independent
     * connection metrics. It keeps time components separated so dominance and
     * choice are evaluated in the same behavioral space as the assignment
     * model, rather than through an already aggregated journey time.
     *
     * IMP(c) =
     *   w_ivt  * IVT(c)
     * + w_acc  * ACC(c)
     * + w_egr  * EGR(c)
     * + w_walk * TWalk(c)
     * + w_twt  * TWait(c)
     * + w_nt   * NT(c)
     * + w_fare * FARE(c).
     *
     * volume_capacity_ratio is parsed and stored for the future
     * capacity-aware search layer. It must not be applied inside the current
     * branch-and-bound search until capacity costs are fixed exogenously for a
     * search iteration.
     */
    struct SearchImpedance final {
        Dimless           in_vehicle_time{};
        Dimless           access_time{};
        Dimless           egress_time{};
        Dimless           transfer_walk_time{};
        Dimless           transfer_wait_time{};
        Dimless           transfer_count{};
        Dimless           fare{};
        Dimless           volume_capacity_ratio{};
        FareNormalization fare_normalization{};
    };

}  // namespace timetable::domain
