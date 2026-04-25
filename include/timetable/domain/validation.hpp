#pragma once

#include <cmath>
#include <string>
#include <string_view>
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

    [[nodiscard]] inline mathfp::Unexpected fail(
          std::string_view invariant
        , mathfp::Error    err
    ) {
        return mathfp::unexpected(
            std::move(err).ctx("validation_invariant", std::string(invariant))
        );
    }

    namespace detail {

        template <class Predicate>
        inline mathfp::Expected<mathfp::Unit> ensure_scalar_satisfies(
              double               value
            , const char*          name
            , const char*          non_finite_message
            , const char*          predicate_message
            , Predicate&&          predicate
        ) {
            if (!is_finite(value)) {
                return fail(
                      non_finite_message
                    , mathfp::invalid_arg(non_finite_message).ctx("name", name)
                );
            }

            if (!std::forward<Predicate>(predicate)(value)) {
                return fail(
                      predicate_message
                    , mathfp::invalid_arg(predicate_message).ctx("name", name)
                );
            }

            return mathfp::kUnit;
        }

    }  // namespace detail

    inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
          mathfp::units::Quantity<double, mathfp::units::Time> v
        , const char*                                          name
    ) {
        return detail::ensure_scalar_satisfies(
              v.value()
            , name
            , "time is not finite"
            , "time must be non-negative"
            , [](double x) { return x >= 0.0; }
        );
    }

    inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
          mathfp::units::Quantity<double, mathfp::units::Length> v
        , const char*                                            name
    ) {
        return detail::ensure_scalar_satisfies(
              v.value()
            , name
            , "length is not finite"
            , "length must be non-negative"
            , [](double x) { return x >= 0.0; }
        );
    }

    inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
          mathfp::units::Quantity<double, mathfp::units::Dimless> v
        , const char*                                             name
    ) {
        return detail::ensure_scalar_satisfies(
              mathfp::units::as_dimless(v)
            , name
            , "coefficient is not finite"
            , "coefficient must be non-negative"
            , [](double x) { return x >= 0.0; }
        );
    }

    inline mathfp::Expected<mathfp::Unit> ensure_positive(
          mathfp::units::Quantity<double, mathfp::units::Dimless> v
        , const char*                                             name
    ) {
        return detail::ensure_scalar_satisfies(
              mathfp::units::as_dimless(v)
            , name
            , "coefficient is not finite"
            , "coefficient must be positive"
            , [](double x) { return x > 0.0; }
        );
    }

    inline mathfp::Expected<mathfp::Unit> ensure_positive(
          Speed       v
        , const char* name
    ) {
        return detail::ensure_scalar_satisfies(
              v.value()
            , name
            , "speed is not finite"
            , "speed must be positive"
            , [](double x) { return x > 0.0; }
        );
    }

}  // namespace timetable::domain::validation
