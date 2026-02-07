#pragma once

#include <cmath>

#include <mathfp/types/units.hpp>

#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain {

    /**
     * @brief Normalized fare contribution for impedance calculations.
     *
     * Missing fares are treated as 0.
     */
    [[nodiscard]] inline double fare_for_impedance(
        const ConnectionSegment& segment
        , double fare_scale
    ) noexcept {
        if (!segment.fare || fare_scale <= 0.0) {
            return 0.0;
        }
        return *segment.fare / fare_scale;
    }

    /**
     * @brief Compute impedance using normalized fare.
     *
     * Missing fare contributes 0.
     */
    [[nodiscard]] inline double connection_impedance(
        Time journey_time
        , TransferCount transfers
        , const ConnectionSegment& segment
        , const SearchImpedance& weights
        , double fare_scale
    ) noexcept {
        const auto jt = journey_time.value();
        const auto nt = static_cast<double>(transfers.get());
        const auto fare = fare_for_impedance(segment, fare_scale);
        const auto a_jt = mathfp::units::as_dimless(weights.a_journey_time);
        const auto a_nt = mathfp::units::as_dimless(weights.a_transfers);
        const auto a_fare = mathfp::units::as_dimless(weights.a_fare);
        return a_jt * jt + a_nt * nt + a_fare * fare;
    }

}  // namespace timetable::domain
