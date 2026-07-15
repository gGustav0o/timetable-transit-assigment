#pragma once

#include <cmath>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    class NonNegativeScalar final {
    public:
        constexpr NonNegativeScalar() = default;

        [[nodiscard]] constexpr double value() const noexcept {
            return value_;
        }

    private:
        constexpr explicit NonNegativeScalar(double value) noexcept
            : value_(value) {}

        double value_{};

        friend mathfp::Expected<NonNegativeScalar> make_non_negative_scalar(
              double
            , const char*
        );
    };

    class PositiveScalar final {
    public:
        constexpr PositiveScalar() noexcept
            : value_(1.0) {}

        [[nodiscard]] constexpr double value() const noexcept {
            return value_;
        }

    private:
        constexpr explicit PositiveScalar(double value) noexcept
            : value_(value) {}

        double value_{ 1.0 };

        friend mathfp::Expected<PositiveScalar> make_positive_scalar(
              double
            , const char*
        );
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
        return NonNegativeScalar{ value };
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
        return PositiveScalar{ value };
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
