#pragma once

#include <concepts>
#include <limits>
#include <source_location>
#include <type_traits>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>

namespace mathfp {

    namespace checked_arithmetic_detail {

        template <class T>
        using Raw = std::remove_cvref_t<T>;

        template <class T>
        concept CharacterIntegral =
               std::same_as<Raw<T>, char>
            || std::same_as<Raw<T>, signed char>
            || std::same_as<Raw<T>, unsigned char>
#ifdef __cpp_char8_t
            || std::same_as<Raw<T>, char8_t>
#endif
            || std::same_as<Raw<T>, char16_t>
            || std::same_as<Raw<T>, char32_t>
            || std::same_as<Raw<T>, wchar_t>;

        template <class T>
        concept CheckedIntegral =
               std::integral<Raw<T>>
            && !std::same_as<Raw<T>, bool>
            && !CharacterIntegral<T>;

        template <CheckedIntegral T>
        MATHFP_NODISCARD inline Unexpected checked_overflow(
              const char*          message
            , T                    lhs
            , T                    rhs
            , std::source_location where
        ) {
            return unexpected(
                overflow_error(message, where)
                .ctx("lhs", lhs)
                .ctx("rhs", rhs));
        }

        template <CheckedIntegral T>
        MATHFP_NODISCARD inline Unexpected checked_underflow(
              const char*          message
            , T                    lhs
            , T                    rhs
            , std::source_location where
        ) {
            return unexpected(
                underflow_error(message, where)
                .ctx("lhs", lhs)
                .ctx("rhs", rhs));
        }

    }  // namespace checked_arithmetic_detail

    template <checked_arithmetic_detail::CheckedIntegral T>
    MATHFP_NODISCARD inline Expected<T> checked_add(
          T                    lhs
        , T                    rhs
        , std::source_location where = std::source_location::current()
    ) {
        using Limits = std::numeric_limits<T>;

        if constexpr (std::is_unsigned_v<T>) {
            if (lhs > Limits::max() - rhs) {
                return checked_arithmetic_detail::checked_overflow(
                    "integer addition overflow", lhs, rhs, where);
            }
        } else {
            if (rhs > 0 && lhs > Limits::max() - rhs) {
                return checked_arithmetic_detail::checked_overflow(
                    "integer addition overflow", lhs, rhs, where);
            }
            if (rhs < 0 && lhs < Limits::min() - rhs) {
                return checked_arithmetic_detail::checked_underflow(
                    "integer addition underflow", lhs, rhs, where);
            }
        }
        return lhs + rhs;
    }

    template <checked_arithmetic_detail::CheckedIntegral T>
    MATHFP_NODISCARD inline Expected<T> checked_sub(
          T                    lhs
        , T                    rhs
        , std::source_location where = std::source_location::current()
    ) {
        using Limits = std::numeric_limits<T>;

        if constexpr (std::is_unsigned_v<T>) {
            if (lhs < rhs) {
                return checked_arithmetic_detail::checked_underflow(
                    "integer subtraction underflow", lhs, rhs, where);
            }
        } else {
            if (rhs > 0 && lhs < Limits::min() + rhs) {
                return checked_arithmetic_detail::checked_underflow(
                    "integer subtraction underflow", lhs, rhs, where);
            }
            if (rhs < 0 && lhs > Limits::max() + rhs) {
                return checked_arithmetic_detail::checked_overflow(
                    "integer subtraction overflow", lhs, rhs, where);
            }
        }
        return lhs - rhs;
    }

    template <checked_arithmetic_detail::CheckedIntegral T>
    MATHFP_NODISCARD inline Expected<T> checked_mul(
          T                    lhs
        , T                    rhs
        , std::source_location where = std::source_location::current()
    ) {
        using Limits = std::numeric_limits<T>;

        if (lhs == T{0} || rhs == T{0}) {
            return T{0};
        }

        if constexpr (std::is_unsigned_v<T>) {
            if (lhs > Limits::max() / rhs) {
                return checked_arithmetic_detail::checked_overflow(
                    "integer multiplication overflow", lhs, rhs, where);
            }
        } else {
            if (lhs == Limits::min() && rhs == T{-1}) {
                return checked_arithmetic_detail::checked_overflow(
                    "integer multiplication overflow", lhs, rhs, where);
            }
            if (rhs == Limits::min() && lhs == T{-1}) {
                return checked_arithmetic_detail::checked_overflow(
                    "integer multiplication overflow", lhs, rhs, where);
            }

            if (lhs > 0) {
                if (rhs > 0) {
                    if (lhs > Limits::max() / rhs) {
                        return checked_arithmetic_detail::checked_overflow(
                            "integer multiplication overflow", lhs, rhs, where);
                    }
                } else {
                    if (rhs < Limits::min() / lhs) {
                        return checked_arithmetic_detail::checked_underflow(
                            "integer multiplication underflow", lhs, rhs, where);
                    }
                }
            } else {
                if (rhs > 0) {
                    if (lhs < Limits::min() / rhs) {
                        return checked_arithmetic_detail::checked_underflow(
                            "integer multiplication underflow", lhs, rhs, where);
                    }
                } else {
                    if (lhs < Limits::max() / rhs) {
                        return checked_arithmetic_detail::checked_overflow(
                            "integer multiplication overflow", lhs, rhs, where);
                    }
                }
            }
        }
        return lhs * rhs;
    }

}  // namespace mathfp
