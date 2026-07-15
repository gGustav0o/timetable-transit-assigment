#pragma once

#include <cmath>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    struct NonNegativeScalar final {
        double value{};
    };

    struct PositiveScalar final {
        double value{ 1.0 };
    };

    [[nodiscard]] inline mathfp::Expected<NonNegativeScalar> make_non_negative_scalar(
          double      value
        , const char* name
    ) {
        if (!std::isfinite(value) || value < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("scalar must be finite and non-negative")
                    .ctx("scalar", name)
                    .ctx("value", value)
            );
        }
        return NonNegativeScalar{ .value = value };
    }

    [[nodiscard]] inline mathfp::Expected<PositiveScalar> make_positive_scalar(
          double      value
        , const char* name
    ) {
        if (!std::isfinite(value) || value <= 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("scalar must be finite and positive")
                    .ctx("scalar", name)
                    .ctx("value", value)
            );
        }
        return PositiveScalar{ .value = value };
    }

    [[nodiscard]] inline mathfp::Expected<mathfp::Unit> validate_non_negative_scalar(
          double      value
        , const char* name
    ) {
        MATHFP_TRY(make_non_negative_scalar(value, name));
        return mathfp::kUnit;
    }

    [[nodiscard]] inline mathfp::Expected<mathfp::Unit> validate_positive_scalar(
          double      value
        , const char* name
    ) {
        MATHFP_TRY(make_positive_scalar(value, name));
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
