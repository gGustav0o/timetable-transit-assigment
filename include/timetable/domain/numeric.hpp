#pragma once

#include <cmath>

#include <mathfp/core/numeric_tolerance.hpp>

namespace timetable::domain::numeric {

    [[nodiscard]] inline bool nearly_equal(
        double lhs
        , double rhs
    ) noexcept {
        const auto abs_tol = mathfp::abs_tolerance(lhs, rhs);
        const auto rel_tol = mathfp::rel_tolerance_coeff<double>();
        const auto scale = mathfp::scalar_scale(lhs, rhs);
        return std::abs(lhs - rhs) <= abs_tol + rel_tol * scale;
    }

}  // namespace timetable::domain::numeric
