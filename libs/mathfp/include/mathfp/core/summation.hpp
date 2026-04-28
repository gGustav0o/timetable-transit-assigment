#pragma once

#include "mathfp/compiler_attributes.hpp"

#include <concepts>
#include <functional>
#include <ranges>
#include <type_traits>

namespace mathfp {

    template <std::floating_point T = double>
    class CompensatedSum final {
    public:
        using value_type = T;

        constexpr void add(T value) noexcept {
            const auto y = value - correction_;
            const auto t = sum_ + y;
            correction_ = (t - sum_) - y;
            sum_ = t;
        }

        constexpr CompensatedSum& operator+=(T value) noexcept {
            add(value);
            return *this;
        }

        MATHFP_NODISCARD
        constexpr T value() const noexcept {
            return sum_;
        }

        MATHFP_NODISCARD
        constexpr T correction() const noexcept {
            return correction_;
        }

    private:
        T sum_{};
        T correction_{};
    };

    template <std::ranges::input_range Range>
        requires std::floating_point<
            std::remove_cvref_t<std::ranges::range_value_t<Range>>
        >
    MATHFP_NODISCARD constexpr auto compensated_sum(Range&& range) {
        using T = std::remove_cvref_t<std::ranges::range_value_t<Range>>;
        CompensatedSum<T> acc;
        for (auto&& value : range) {
            acc.add(static_cast<T>(value));
        }
        return acc.value();
    }

    template <
          std::ranges::input_range Range
        , class Projection
        , std::floating_point T = double
    >
    MATHFP_NODISCARD constexpr T compensated_sum_by(
          Range&&     range
        , Projection  projection
    ) {
        CompensatedSum<T> acc;
        for (auto&& item : range) {
            acc.add(static_cast<T>(std::invoke(projection, item)));
        }
        return acc.value();
    }

}  // namespace mathfp
