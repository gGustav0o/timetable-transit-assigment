#pragma once

#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

namespace mathfp::fp {
    namespace detail {

        template <class T>
        struct is_expected : std::false_type {};

        template <class U>
        struct is_expected<tl::expected<U, ::mathfp::Error>> : std::true_type {};

        template <class T>
        inline constexpr bool is_expected_v = is_expected<std::remove_cvref_t<T>>::value;

        template <class T, class F>
        using invoke_result_t = std::invoke_result_t<F, T>;

        template <class R>
        struct map_value { using type = std::remove_cvref_t<R>; };

        template <>
        struct map_value<void> { using type = ::mathfp::Unit; };

        template <class T, class F>
        using map_value_t = typename map_value<invoke_result_t<T, F>>::type;

    }  // namespace detail

    struct identity_fn {
        template <class T>
        constexpr T&& operator()(T&& x) const noexcept {
            return std::forward<T>(x);
        }
    };

    inline constexpr identity_fn identity{};

    template <class F, class G>
    constexpr auto compose(F f, G g) {
        return [f = std::move(f), g = std::move(g)](auto&& x) mutable -> decltype(auto) {
            return std::invoke(f, std::invoke(g, std::forward<decltype(x)>(x)));
            };
    }

    template <class T, class F>
    [[nodiscard]] auto map(::mathfp::Expected<T> e, F&& f)
        -> ::mathfp::Expected<detail::map_value_t<T&&, F>> {
        using U = detail::map_value_t<T&&, F>;
        if (!e) return ::mathfp::unexpected(std::move(e).error());

        if constexpr (std::is_void_v<detail::invoke_result_t<T&&, F>>) {
            std::invoke(std::forward<F>(f), std::move(e).value());
            return ::mathfp::kUnit;
        }
        else {
            return ::mathfp::Expected<U>(std::invoke(std::forward<F>(f), std::move(e).value()));
        }
    }

    template <class T, class F>
    [[nodiscard]] auto and_then(::mathfp::Expected<T> e, F&& f)
        -> std::invoke_result_t<F, T&&> {
        using R = std::invoke_result_t<F, T&&>;
        static_assert(detail::is_expected_v<R>,
            "and_then expects f: T -> Expected<U> (tl::expected<U, Error>)");
        if (!e) return R(::mathfp::unexpected(std::move(e).error()));
        return std::invoke(std::forward<F>(f), std::move(e).value());
    }

    template <class T, class F>
    [[nodiscard]] ::mathfp::Expected<T> map_error(::mathfp::Expected<T> e, F&& f) {
        if (e) return e;
        auto mapped = std::invoke(std::forward<F>(f), std::move(e).error());
        static_assert(std::is_same_v<std::remove_cvref_t<decltype(mapped)>, ::mathfp::Error>,
            "map_error expects f: Error -> Error");
        return ::mathfp::unexpected(std::move(mapped));
    }

    template <class T, class F>
    [[nodiscard]] ::mathfp::Expected<T> or_else(::mathfp::Expected<T> e, F&& f) {
        if (e) return e;
        auto r = std::invoke(std::forward<F>(f), std::move(e).error());
        static_assert(detail::is_expected_v<decltype(r)>,
            "or_else expects f: Error -> Expected<T>");
        return r;
    }

    template <class T, class F>
    [[nodiscard]] ::mathfp::Expected<T> inspect(::mathfp::Expected<T> e, F&& f) {
        if (e) std::invoke(std::forward<F>(f), e.value());
        return e;
    }

    template <class T, class F>
    [[nodiscard]] ::mathfp::Expected<T> inspect_error(::mathfp::Expected<T> e, F&& f) {
        if (!e) std::invoke(std::forward<F>(f), e.error());
        return e;
    }

    template <class T>
    [[nodiscard]] ::mathfp::Expected<T> flatten(::mathfp::Expected<::mathfp::Expected<T>> e) {
        if (!e) return ::mathfp::unexpected(std::move(e).error());
        return std::move(e).value();
    }

    template <class T>
    [[nodiscard]] T value_or(::mathfp::Expected<T> e, T default_value) {
        if (e) return std::move(e).value();
        return default_value;
    }

    template <class T, class F>
    [[nodiscard]] T value_or_else(::mathfp::Expected<T> e, F&& f) {
        if (e) return std::move(e).value();
        return std::invoke(std::forward<F>(f), std::move(e).error());
    }

    namespace pipe {

        template <class Adaptor, class T>
        concept applicable =
            requires(Adaptor && a, ::mathfp::Expected<T> e) {
            std::forward<Adaptor>(a)(std::move(e));
        };

        template <class T, class Adaptor>
            requires applicable<Adaptor, T>
        [[nodiscard]] auto operator|(::mathfp::Expected<T> e, Adaptor&& a) {
            return std::forward<Adaptor>(a)(std::move(e));
        }

        template <class F>
        struct map_t {
            F f;
            template <class T>
            [[nodiscard]] auto operator()(::mathfp::Expected<T> e)&& {
                return ::mathfp::fp::map(std::move(e), std::move(f));
            }
        };

        template <class F>
        [[nodiscard]] inline auto map(F f) { return map_t<F>{std::move(f)}; }

        template <class F>
        struct and_then_t {
            F f;
            template <class T>
            [[nodiscard]] auto operator()(::mathfp::Expected<T> e)&& {
                return ::mathfp::fp::and_then(std::move(e), std::move(f));
            }
        };

        template <class F>
        [[nodiscard]] inline auto and_then(F f) { return and_then_t<F>{std::move(f)}; }

        template <class F>
        struct map_error_t {
            F f;
            template <class T>
            [[nodiscard]] auto operator()(::mathfp::Expected<T> e)&& {
                return ::mathfp::fp::map_error(std::move(e), std::move(f));
            }
        };

        template <class F>
        [[nodiscard]] inline auto map_error(F f) { return map_error_t<F>{std::move(f)}; }

        template <class F>
        struct or_else_t {
            F f;
            template <class T>
            [[nodiscard]] auto operator()(::mathfp::Expected<T> e)&& {
                return ::mathfp::fp::or_else(std::move(e), std::move(f));
            }
        };

        template <class F>
        [[nodiscard]] inline auto or_else(F f) { return or_else_t<F>{std::move(f)}; }

        template <class F>
        struct inspect_t {
            F f;
            template <class T>
            [[nodiscard]] auto operator()(::mathfp::Expected<T> e)&& {
                return ::mathfp::fp::inspect(std::move(e), std::move(f));
            }
        };

        template <class F>
        [[nodiscard]] inline auto inspect(F f) { return inspect_t<F>{std::move(f)}; }

        template <class F>
        struct inspect_error_t {
            F f;
            template <class T>
            [[nodiscard]] auto operator()(::mathfp::Expected<T> e)&& {
                return ::mathfp::fp::inspect_error(std::move(e), std::move(f));
            }
        };

        template <class F>
        [[nodiscard]] inline auto inspect_error(F f) { return inspect_error_t<F>{std::move(f)}; }

    }  // namespace pipe

}  // namespace mathfp::fp
