// include/mathfp/core/applicative.hpp
#pragma once

#include <concepts>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/fp.hpp>

namespace mathfp::app {

    namespace detail {
        template <class F, class... Ts>
        using invoke_result_t = std::invoke_result_t<F, Ts...>;
    }  // namespace detail

    template <class A, class B>
    MATHFP_NODISCARD ::mathfp::Expected<std::pair<A, B>> product(
        ::mathfp::Expected<A> a
        , ::mathfp::Expected<B> b
    ) {
        if (!a) return ::mathfp::unexpected(std::move(a).error());
        if (!b) return ::mathfp::unexpected(std::move(b).error());
        return std::pair<A, B>{std::move(a).value(), std::move(b).value()};
    }

    template <class F, class A>
    MATHFP_NODISCARD auto ap(
        ::mathfp::Expected<F> ef
        , ::mathfp::Expected<A> ea
    ) -> ::mathfp::Expected<detail::invoke_result_t<F, A>> {
        using B = detail::invoke_result_t<F, A>;
        if (!ef) return ::mathfp::unexpected(std::move(ef).error());
        if (!ea) return ::mathfp::unexpected(std::move(ea).error());
        return ::mathfp::Expected<B>(std::invoke(std::move(ef).value(), std::move(ea).value()));
    }

    template <class F, class A, class B>
    MATHFP_NODISCARD auto lift2(
        F&& f
        , ::mathfp::Expected<A> a
        , ::mathfp::Expected<B> b
    ) -> ::mathfp::Expected<detail::invoke_result_t<F, A, B>> {
        using C = detail::invoke_result_t<F, A, B>;
        if (!a) return ::mathfp::unexpected(std::move(a).error());
        if (!b) return ::mathfp::unexpected(std::move(b).error());
        return ::mathfp::Expected<C>(std::invoke(std::forward<F>(f), std::move(a).value(), std::move(b).value()));
    }

    template <class F, class A, class B, class C>
    MATHFP_NODISCARD auto lift3(
        F&& f
        , ::mathfp::Expected<A> a
        , ::mathfp::Expected<B> b
        , ::mathfp::Expected<C> c
    ) -> ::mathfp::Expected<detail::invoke_result_t<F, A, B, C>> {
        using D = detail::invoke_result_t<F, A, B, C>;
        if (!a) return ::mathfp::unexpected(std::move(a).error());
        if (!b) return ::mathfp::unexpected(std::move(b).error());
        if (!c) return ::mathfp::unexpected(std::move(c).error());
        return ::mathfp::Expected<D>(std::invoke(std::forward<F>(f),
            std::move(a).value(),
            std::move(b).value(),
            std::move(c).value()));
    }

}  // namespace mathfp::app
