#pragma once

#include <cmath>
#include <tuple>

#include <mathfp/types/units.hpp>

#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain {
    inline double normalized_fare_value(
          double   fare
        , double fare_scale
    ) noexcept {
        if (fare_scale <= 0.0) {
            return 0.0;
        }
        return fare / fare_scale;
    }

    /**
     * @brief Normalized fare contribution for impedance calculations.
     *
     * Missing fares are treated as 0.
     */
    inline double fare_for_impedance(
          const ConnectionSegment& segment
        , double                 fare_scale
    ) noexcept {
        if (!segment.fare || fare_scale <= 0.0) {
            return 0.0;
        }
        return normalized_fare_value(*segment.fare, fare_scale);
    }

    inline std::tuple<double, double, double, double, double, double>
    extract_connection_impedance_inputs(
          Time                       journey_time
        , TransferCount            transfers
        , const ConnectionSegment& segment
        , const SearchImpedance&   weights
        , double                   fare_scale
    ) noexcept {
        return {
              mathfp::units::as_dimless(weights.a_journey_time)
            , mathfp::units::as_dimless(weights.a_transfers)
            , mathfp::units::as_dimless(weights.a_fare)
            , journey_time.value()
            , static_cast<double>(transfers.get())
            , fare_for_impedance(segment, fare_scale)
        };
    }

    inline double linear_connection_impedance(
          double   a_jt
        , double a_nt
        , double a_fare
        , double jt
        , double nt
        , double fare
    ) noexcept {
        return a_jt * jt + a_nt * nt + a_fare * fare;
    }

    inline double connection_impedance_value(
          Time                     journey_time
        , TransferCount          transfers
        , double                 fare
        , const SearchImpedance& weights
        , double                 fare_scale
    ) noexcept {
        return linear_connection_impedance(
              mathfp::units::as_dimless(weights.a_journey_time)
            , mathfp::units::as_dimless(weights.a_transfers)
            , mathfp::units::as_dimless(weights.a_fare)
            , journey_time.value()
            , static_cast<double>(transfers.get())
            , normalized_fare_value(fare, fare_scale)
        );
    }

    /**
     * @brief Compute impedance using normalized fare.
     *
     * Missing fare contributes 0.
     */
    inline double connection_impedance(
          Time                       journey_time
        , TransferCount            transfers
        , const ConnectionSegment& segment
        , const SearchImpedance&   weights
        , double                   fare_scale
    ) noexcept {
        const auto [a_jt, a_nt, a_fare, jt, nt, fare] =
            extract_connection_impedance_inputs(
                  journey_time
                , transfers
                , segment
                , weights
                , fare_scale
            );
        return linear_connection_impedance(
              a_jt
            , a_nt
            , a_fare
            , jt
            , nt
            , fare
        );
    }

}  // namespace timetable::domain
