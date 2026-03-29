#pragma once

#include <cmath>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    /**
     * @brief Additive fare charged for one timed ride on a line.
     *
     * Raw timetable preprocessing derives ConnectionSegment::fare from this
     * service-level value. Pre-segmented inputs bypass this model and carry fare
     * directly on connection segments.
     */
    struct LineFare final {
        double ride_fare{};
    };

    inline mathfp::Expected<mathfp::Unit> ensure_line_fare_consistent(
        LineFare fare
    ) {
        if (!std::isfinite(fare.ride_fare)) {
            const char* message = "line fare is not finite";
            return validation::fail(
                  message
                , mathfp::invalid_arg(message).ctx("ride_fare", fare.ride_fare)
            );
        }

        if (fare.ride_fare < 0.0) {
            const char* message = "line fare must be non-negative";
            return validation::fail(
                  message
                , mathfp::invalid_arg(message).ctx("ride_fare", fare.ride_fare)
            );
        }

        return mathfp::kUnit;
    }

    inline double line_ride_fare_value(
        LineFare fare
    ) noexcept {
        return fare.ride_fare;
    }

}  // namespace timetable::domain
