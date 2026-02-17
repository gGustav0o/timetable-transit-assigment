#pragma once

#include <cassert>
#include <cmath>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/scalars.hpp"

namespace timetable::domain::validation {

    inline bool is_finite(double v) noexcept {
        return std::isfinite(v);
    }

    inline mathfp::Unexpected fail(
        const char* assert_msg
        , mathfp::Error err
    ) {
        assert(false && "validation failed");
        return mathfp::unexpected(std::move(err).ctx("assert", assert_msg));
    }

    inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
        mathfp::units::Quantity<double, mathfp::units::Time> v
        , const char* name
    ) {
        const auto x = v.value();
        if (!is_finite(x))
            return fail(
                "time is not finite"
                , mathfp::invalid_arg("time is not finite").ctx("name", name)
            );

        if (x < 0.0)
            return fail(
                "time must be non-negative"
                , mathfp::invalid_arg("time must be non-negative").ctx("name", name)
            );
        return mathfp::kUnit;
    }

    inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
        mathfp::units::Quantity<double, mathfp::units::Length> v
        , const char* name
    ) {
        const auto x = v.value();
        if (!is_finite(x))
            return fail(
                "length is not finite"
                , mathfp::invalid_arg("length is not finite").ctx("name", name)
            );

        if (x < 0.0)
            return fail(
                "length must be non-negative"
                , mathfp::invalid_arg("length must be non-negative").ctx("name", name)
            );
        return mathfp::kUnit;
    }

    inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
        mathfp::units::Quantity<double, mathfp::units::Dimless> v
        , const char* name
    ) {
        const auto x = mathfp::units::as_dimless(v);
        if (!is_finite(x))
            return fail(
                "coefficient is not finite"
                , mathfp::invalid_arg("coefficient is not finite").ctx("name", name)
            );

        if (x < 0.0)
            return fail(
                "coefficient must be non-negative"
                , mathfp::invalid_arg("coefficient must be non-negative").ctx("name", name)
            );
        return mathfp::kUnit;
    }

    inline mathfp::Expected<mathfp::Unit> ensure_positive(
        mathfp::units::Quantity<double, mathfp::units::Dimless> v
        , const char* name
    ) {
        const auto x = mathfp::units::as_dimless(v);
        if (!is_finite(x))
            return fail(
                "coefficient is not finite"
                , mathfp::invalid_arg("coefficient is not finite").ctx("name", name)
            );

        if (x <= 0.0)
            return fail(
                "coefficient must be positive"
                , mathfp::invalid_arg("coefficient must be positive").ctx("name", name)
            );
        return mathfp::kUnit;
    }

    inline mathfp::Expected<mathfp::Unit> ensure_positive(
        Speed v
        , const char* name
    ) {
        const auto x = v.value();
        if (!is_finite(x))
            return fail(
                "speed is not finite"
                , mathfp::invalid_arg("speed is not finite").ctx("name", name)
            );

        if (x <= 0.0)
            return fail(
                "speed must be positive"
                , mathfp::invalid_arg("speed must be positive").ctx("name", name)
            );
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::validation
