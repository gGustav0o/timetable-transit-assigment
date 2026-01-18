#pragma once

#include <type_traits>
#include <concepts>
#include <source_location>
#include <string_view>
#include <utility>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/unit.hpp>

namespace mathfp::detail {

	template <class... Ts>
	struct Overloaded : Ts... {
		using Ts::operator()...;
	};

	template <class... Ts>
	Overloaded(Ts...) -> Overloaded<Ts...>;

	template <class>
	inline constexpr bool kAlwaysFalse = false;

} // namespace detail

namespace mathfp {

	template <class... Ts>
	MATHFP_NODISCARD constexpr auto overloaded(Ts&&... ts) {
		return detail::Overloaded<std::decay_t<Ts>...>{std::forward<Ts>(ts)...};
	}

    MATHFP_NODISCARD inline Expected<Unit> ensure(bool ok, Error err) {
        if (!ok) return unexpected(std::move(err));
        return kUnit;
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_eq(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)
    {
        using C = std::common_type_t<A, B>;
        const C ac = static_cast<C>(a);
        const C bc = static_cast<C>(b);
        if (mathfp::almost_equal(ac, bc)) return kUnit;
        return unexpected(
            invalid_arg("dimension mismatch", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_eq(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires requires { a == b; }
              && (!std::is_arithmetic_v<A> || !std::is_arithmetic_v<B>)
    {
        if (a == b) return kUnit;
        return unexpected(
            invalid_arg("dimension mismatch", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_le(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)
    {
        using C = std::common_type_t<A, B>;
        const C ac = static_cast<C>(a);
        const C bc = static_cast<C>(b);
        if (mathfp::leq_tol(ac, bc)) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected <= ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_le(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires requires { a <= b; }
              && (!std::is_arithmetic_v<A> || !std::is_arithmetic_v<B>)
    {
        if (a <= b) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected <= ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_ge(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)
    {
        using C = std::common_type_t<A, B>;
        const C ac = static_cast<C>(a);
        const C bc = static_cast<C>(b);
        if (mathfp::leq_tol(bc, ac)) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected >= ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_ge(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires requires { a >= b; }
              && (!std::is_arithmetic_v<A> || !std::is_arithmetic_v<B>)
    {
        if (a >= b) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected >= ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_lt(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)
    {
        using C = std::common_type_t<A, B>;
        const C ac = static_cast<C>(a);
        const C bc = static_cast<C>(b);
        if ((ac < bc) && !mathfp::almost_equal(ac, bc)) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected < ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_lt(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires requires { a < b; }
              && (!std::is_arithmetic_v<A> || !std::is_arithmetic_v<B>)
    {
        if (a < b) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected < ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_gt(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires (std::is_arithmetic_v<A> && std::is_arithmetic_v<B>)
    {
        using C = std::common_type_t<A, B>;
        const C ac = static_cast<C>(a);
        const C bc = static_cast<C>(b);
        if ((ac > bc) && !mathfp::almost_equal(ac, bc)) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected > ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    template <class A, class B>
    MATHFP_NODISCARD inline Expected<Unit> ensure_gt(
        const A& a
        , const B& b
        , std::string_view a_name
        , std::string_view b_name
        , std::source_location where = std::source_location::current())
        requires requires { a > b; }
              && (!std::is_arithmetic_v<A> || !std::is_arithmetic_v<B>)
    {
        if (a > b) return kUnit;
        return unexpected(
            invalid_arg("constraint violated: expected > ", where)
            .ctx(a_name, a)
            .ctx(b_name, b));
    }

    // Частые "математические" проверки: ноль, положительность и т.п.
    template <class T>
    MATHFP_NODISCARD inline Expected<Unit> ensure_nonzero(
        const T& x
        , std::string_view name
        , std::source_location where = std::source_location::current())
        requires std::is_arithmetic_v<T>
    {
        if (mathfp::not_zero(x)) return kUnit;
        return unexpected(domain_error("value must be non-zero", where).ctx(name, x));
    }

    template <class T>
    MATHFP_NODISCARD inline Expected<Unit> ensure_nonzero(
        const T& x
        , std::string_view name
        , std::source_location where = std::source_location::current())
        requires requires { x == T{ 0 }; }
              && (!std::is_arithmetic_v<T>)
    {
        if (!(x == T{ 0 })) return kUnit;
        return unexpected(domain_error("value must be non-zero", where).ctx(name, x));
    }

    template <class T>
    MATHFP_NODISCARD inline Expected<Unit> ensure_positive(
        const T& x
        , std::string_view name
        , std::source_location where = std::source_location::current())
        requires std::is_arithmetic_v<T>
    {
        if (mathfp::greater_than_zero(x)) return kUnit;
        return unexpected(domain_error("value must be positive", where).ctx(name, x));
    }

    template <class T>
    MATHFP_NODISCARD inline Expected<Unit> ensure_positive(
        const T& x
        , std::string_view name
        , std::source_location where = std::source_location::current())
        requires requires { x > T{ 0 }; }
              && (!std::is_arithmetic_v<T>)
    {
        if (x > T{ 0 }) return kUnit;
        return unexpected(domain_error("value must be positive", where).ctx(name, x));
    }

} // namespace mathfp
