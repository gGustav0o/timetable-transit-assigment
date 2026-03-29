#pragma once

#include "mathfp/compiler_attributes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <concepts>
#include <type_traits>

namespace mathfp {
    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr T machine_epsilon() noexcept {
        return std::numeric_limits<T>::epsilon();
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr T unit_roundoff() noexcept {
        return mathfp::machine_epsilon<T>() / T(2);
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr T scalar_scale(T a, T b) noexcept {
        return (std::max)({ T(1), std::abs(a), std::abs(b) });
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr T abs_tolerance_scaled(T scale, T k = T(1)) noexcept {
        return k * mathfp::machine_epsilon<T>() * (std::max)(T(1), scale);
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr T abs_tolerance(T ref) noexcept {
        using std::abs;
        return mathfp::abs_tolerance_scaled<T>((std::max)(T(1), abs(ref)));
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr T abs_tolerance(T a, T b) noexcept {
        return mathfp::abs_tolerance_scaled<T>(scalar_scale(a, b));
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr T rel_tolerance_coeff() noexcept {
        return mathfp::machine_epsilon<T>();
    }


    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr bool almost_equal(T a, T b, T abs_tol, T rel_tol) noexcept {
        if (!std::isfinite(a) || !std::isfinite(b)) {
            if (std::isnan(a) || std::isnan(b)) return false;
            return a == b;

        }
        const T diff  = std::abs(a - b);
        const T scale = mathfp::scalar_scale(a, b);
        const T tol   = (std::max)(abs_tol, rel_tol * scale);
        return diff <= tol;
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr bool almost_equal(T a, T b) noexcept {
        return mathfp::almost_equal(
              a
            , b
            , mathfp::abs_tolerance<T>(T(0))
            , mathfp::rel_tolerance_coeff<T>()
        );
    }

    template<std::integral I>
    MATHFP_CONST_FN
    constexpr bool almost_equal(I a, I b) noexcept {
        return a == b;
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr bool almost_zero(T x) noexcept {
        return std::abs(x) <= mathfp::abs_tolerance<T>(T(0));
    }

    template<std::integral I>
    MATHFP_CONST_FN
    constexpr bool almost_zero(I x) noexcept {
        return x == 0;
    }

    template<typename T>
    MATHFP_CONST_FN
    constexpr bool not_zero(T x) noexcept { return !almost_zero(x); }

    template<std::integral T>
    constexpr bool not_zero(T x) noexcept { return x != 0; }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr bool greater_than_zero(T x) noexcept {
        return x > mathfp::abs_tolerance<T>(T(0));
    }

    template<std::integral I>
    MATHFP_CONST_FN
    constexpr bool greater_than_zero(I x) noexcept {
        return x > 0;
    }

    template<std::floating_point T>
    MATHFP_CONST_FN
    constexpr bool less_than_zero(T x) noexcept {
        return x < -mathfp::abs_tolerance<T>(T(0));
    }

    template<std::signed_integral I>
    MATHFP_CONST_FN
    constexpr bool less_than_zero(I x) noexcept {
        return x < 0;
    }

    template<std::unsigned_integral I>
    MATHFP_CONST_FN
    constexpr bool less_than_zero(I) noexcept {
        return false;
    }

    template<typename T>
    MATHFP_CONST_FN
    constexpr bool leq_tol(T a, T b) noexcept {
        return (a <= b) || mathfp::almost_equal(a, b);
    }

    MATHFP_CONST_FN
    inline double extent_scale(double hx, double hy) noexcept {
        return (std::max)(hx, hy);
    }

    MATHFP_CONST_FN
    inline double swap_tolerance(double hx, double hy) noexcept {
        return mathfp::abs_tolerance(mathfp::extent_scale(hx, hy));
    }

}
