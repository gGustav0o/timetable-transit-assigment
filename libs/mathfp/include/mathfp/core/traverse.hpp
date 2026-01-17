#pragma once

#include <cstddef>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include <mathfp/core/expected.hpp>

namespace mathfp::trv {
    namespace detail {

        template <class>
        struct expected_value {};

        template <class U>
        struct expected_value<tl::expected<U, ::mathfp::Error>> {
            using type = U;
        };

        template <class E>
        concept expected =
            requires { typename expected_value<std::remove_cvref_t<E>>::type; };

        template <class E>
        using expected_value_t = typename expected_value<std::remove_cvref_t<E>>::type;

    }  // namespace detail

    template <std::ranges::input_range R, class F>
    [[nodiscard]] auto traverse(R&& r, F&& f)
        -> ::mathfp::Expected<std::vector<detail::expected_value_t<std::invoke_result_t<F, std::ranges::range_reference_t<R>>>>> {
        using XRef = std::ranges::range_reference_t<R>;
        using ERet = std::invoke_result_t<F, XRef>;
        static_assert(detail::expected<ERet>, "traverse expects f: X -> Expected<Y>");

        using Y = detail::expected_value_t<ERet>;
        std::vector<Y> out;

        if constexpr (std::ranges::sized_range<R>) {
            out.reserve(static_cast<std::size_t>(std::ranges::size(r)));
        }

        std::size_t i = 0;
        for (auto&& x : r) {
            auto ey = std::invoke(std::forward<F>(f), x);
            if (!ey) {
                auto err = std::move(ey).error();
                err.ctx("index", i);
                return ::mathfp::unexpected(std::move(err));
            }
            out.push_back(std::move(ey).value());
            ++i;
        }

        return out;
    }

    template <std::ranges::input_range R>
    [[nodiscard]] auto sequence(R&& r)
        -> ::mathfp::Expected<std::vector<detail::expected_value_t<std::ranges::range_value_t<R>>>> {
        using E = std::ranges::range_value_t<R>;
        static_assert(detail::expected<E>, "sequence expects a range of Expected<T>");

        using T = detail::expected_value_t<E>;
        std::vector<T> out;

        if constexpr (std::ranges::sized_range<R>) {
            out.reserve(static_cast<std::size_t>(std::ranges::size(r)));
        }

        std::size_t i = 0;
        for (auto&& e : r) {
            if (!e) {
                auto err = std::move(e).error();
                err.ctx("index", i);
                return ::mathfp::unexpected(std::move(err));
            }
            out.push_back(std::move(e).value());
            ++i;
        }

        return out;
    }

}  // namespace mathfp::trv
