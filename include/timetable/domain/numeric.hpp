#pragma once

#include <cmath>

#include <mathfp/core/numeric_tolerance.hpp>

namespace timetable::domain::numeric {

    [[nodiscard]] inline double positive_stability_floor() noexcept {
        return mathfp::abs_tolerance(0.0);
    }

}  // namespace timetable::domain::numeric
