#pragma once

#include <cmath>
#include <compare>
#include <concepts>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

namespace mathfp::units {

    template <int L, int M, int Ti, int I, int Th, int N, int J>
    struct Dim final {
        static constexpr int kL  = L;
        static constexpr int kM  = M;
        static constexpr int kTi = Ti;
        static constexpr int kI  = I;
        static constexpr int kTh = Th;
        static constexpr int kN  = N;
        static constexpr int kJ  = J;
    };

    using Dimless = Dim<0, 0, 0, 0, 0, 0, 0>;

    namespace detail {

        template <class D>
        concept dim = requires {
            D::kL; D::kM; D::kTi; D::kI; D::kTh; D::kN; D::kJ;
        };

        template <class A, class B>
        struct dim_add;

        template <int L1, int M1, int T1, int I1, int Th1, int N1, int J1,
            int L2, int M2, int T2, int I2, int Th2, int N2, int J2>
        struct dim_add<Dim<L1, M1, T1, I1, Th1, N1, J1>, Dim<L2, M2, T2, I2, Th2, N2, J2>> {
            using type = Dim<L1 + L2, M1 + M2, T1 + T2, I1 + I2, Th1 + Th2, N1 + N2, J1 + J2>;
        };

        template <class A, class B>
        using dim_add_t = typename dim_add<A, B>::type;

        template <class A, class B>
        struct dim_sub;

        template <int L1, int M1, int T1, int I1, int Th1, int N1, int J1,
            int L2, int M2, int T2, int I2, int Th2, int N2, int J2>
        struct dim_sub<Dim<L1, M1, T1, I1, Th1, N1, J1>, Dim<L2, M2, T2, I2, Th2, N2, J2>> {
            using type = Dim<L1 - L2, M1 - M2, T1 - T2, I1 - I2, Th1 - Th2, N1 - N2, J1 - J2>;
        };

        template <class A, class B>
        using dim_sub_t = typename dim_sub<A, B>::type;

        template <class D, int P>
        struct dim_pow;

        template <int L, int M, int T, int I, int Th, int N, int J, int P>
        struct dim_pow<Dim<L, M, T, I, Th, N, J>, P> {
            using type = Dim<L* P, M* P, T* P, I* P, Th* P, N* P, J* P>;
        };

        template <class D, int P>
        using dim_pow_t = typename dim_pow<D, P>::type;

        template <class D>
        struct dim_half;

        template <int L, int M, int T, int I, int Th, int N, int J>
        struct dim_half<Dim<L, M, T, I, Th, N, J>> {
            static_assert((L % 2) == 0 && (M % 2) == 0 && (T % 2) == 0 &&
                (I % 2) == 0 && (Th % 2) == 0 && (N % 2) == 0 &&
                (J % 2) == 0,
                "sqrt is only defined for dimensions with even exponents");
            using type = Dim<L / 2, M / 2, T / 2, I / 2, Th / 2, N / 2, J / 2>;
        };

        template <class D>
        using dim_half_t = typename dim_half<D>::type;

        template <class D>
        inline constexpr bool is_dimless_v =
            D::kL == 0 && D::kM == 0 && D::kTi == 0 && D::kI == 0 &&
            D::kTh == 0 && D::kN == 0 && D::kJ == 0;

    }  // namespace detail

    template <class Rep, detail::dim D>
    class Quantity final {
    public:
        using RepType = Rep;
        using DimType = D;

        constexpr Quantity() = default;
        constexpr explicit Quantity(Rep v) : value_(std::move(v)) {}

        [[nodiscard]] constexpr const Rep& value() const& noexcept { return value_; }
        [[nodiscard]] constexpr Rep& value_mut() & noexcept { return value_; }
        [[nodiscard]] constexpr Rep&& value() && noexcept { return std::move(value_); }

    private:
        Rep value_{};
    };

    using Length  = Dim<1, 0, 0, 0, 0, 0, 0>;
    using Mass    = Dim<0, 1, 0, 0, 0, 0, 0>;
    using Time    = Dim<0, 0, 1, 0, 0, 0, 0>;
    using Current = Dim<0, 0, 0, 1, 0, 0, 0>;
    using Temp    = Dim<0, 0, 0, 0, 1, 0, 0>;
    using Amount  = Dim<0, 0, 0, 0, 0, 1, 0>;
    using LumInt  = Dim<0, 0, 0, 0, 0, 0, 1>;

    template <class Rep>
    [[nodiscard]] constexpr Quantity<Rep, Length> meters(Rep v) { return Quantity<Rep, Length>(std::move(v)); }

    template <class Rep>
    [[nodiscard]] constexpr Quantity<Rep, Time> seconds(Rep v) { return Quantity<Rep, Time>(std::move(v)); }

    template <class Rep>
    [[nodiscard]] constexpr Quantity<Rep, Mass> kilograms(Rep v) { return Quantity<Rep, Mass>(std::move(v)); }

    template <class Rep>
    [[nodiscard]] constexpr Quantity<Rep, detail::dimless> dimless(Rep v) { return Quantity<Rep, Dimless>(std::move(v)); }


    template <class R1, class R2, detail::dim D>
    [[nodiscard]] constexpr auto operator+(const Quantity<R1, D>& a, const Quantity<R2, D>& b) {
        using R = std::common_type_t<R1, R2>;
        return Quantity<R, D>(static_cast<R>(a.value()) + static_cast<R>(b.value()));
    }

    template <class R1, class R2, detail::dim D>
    [[nodiscard]] constexpr auto operator-(const Quantity<R1, D>& a, const Quantity<R2, D>& b) {
        using R = std::common_type_t<R1, R2>;
        return Quantity<R, D>(static_cast<R>(a.value()) - static_cast<R>(b.value()));
    }

    template <class R, detail::dim D>
    [[nodiscard]] constexpr auto operator-(const Quantity<R, D>& x) {
        return Quantity<R, D>(-x.value());
    }

    template <class R1, class R2, detail::dim D>
    [[nodiscard]] constexpr bool operator==(const Quantity<R1, D>& a, const Quantity<R2, D>& b) {
        using R = std::common_type_t<R1, R2>;
        return static_cast<R>(a.value()) == static_cast<R>(b.value());
    }

    template <class R1, class R2, detail::dim D>
    [[nodiscard]] constexpr auto operator<=>(const Quantity<R1, D>& a, const Quantity<R2, D>& b) {
        using R = std::common_type_t<R1, R2>;
        return static_cast<R>(a.value()) <=> static_cast<R>(b.value());
    }

    template <class R1, class R2, detail::dim D1, detail::dim D2>
    [[nodiscard]] constexpr auto operator*(const Quantity<R1, D1>& a, const Quantity<R2, D2>& b) {
        using R = std::common_type_t<R1, R2>;
        using D = detail::dim_add_t<D1, D2>;
        return Quantity<R, D>(static_cast<R>(a.value()) * static_cast<R>(b.value()));
    }

    template <class R1, class R2, detail::dim D1, detail::dim D2>
    [[nodiscard]] constexpr auto operator/(const Quantity<R1, D1>& a, const Quantity<R2, D2>& b) {
        using R = std::common_type_t<R1, R2>;
        using D = detail::dim_sub_t<D1, D2>;
        return Quantity<R, D>(static_cast<R>(a.value()) / static_cast<R>(b.value()));
    }

    template <class S, class R, detail::dim D>
        requires std::is_arithmetic_v<std::remove_cvref_t<S>>
    [[nodiscard]] constexpr auto operator*(S s, const Quantity<R, D>& q) {
        using RR = std::common_type_t<std::remove_cvref_t<S>, R>;
        return Quantity<RR, D>(static_cast<RR>(s) * static_cast<RR>(q.value()));
    }

    template <class S, class R, detail::dim D>
        requires std::is_arithmetic_v<std::remove_cvref_t<S>>
    [[nodiscard]] constexpr auto operator*(const Quantity<R, D>& q, S s) {
        return s * q;
    }

    template <class S, class R, detail::dim D>
        requires std::is_arithmetic_v<std::remove_cvref_t<S>>
    [[nodiscard]] constexpr auto operator/(const Quantity<R, D>& q, S s) {
        using RR = std::common_type_t<R, std::remove_cvref_t<S>>;
        return Quantity<RR, D>(static_cast<RR>(q.value()) / static_cast<RR>(s));
    }

    template <int P, class R, detail::dim D>
    [[nodiscard]] constexpr auto pow(const Quantity<R, D>& q) {
        using DD = detail::dim_pow_t<D, P>;
        using RR = R;
        if constexpr (P == 0) {
            return Quantity<RR, Dimless>(RR{ 1 });
        }
        else if constexpr (P == 1) {
            return Quantity<RR, D>(q.value());
        }
        else if constexpr (P == 2) {
            return Quantity<RR, DD>(q.value() * q.value());
        }
        else {
            return Quantity<RR, DD>(static_cast<RR>(std::pow(q.value(), static_cast<RR>(P))));
        }
    }

    template <class R, detail::dim D>
    [[nodiscard]] inline auto sqrt(const Quantity<R, D>& q) {
        using DD = detail::dim_half_t<D>;
        return Quantity<R, DD>(static_cast<R>(std::sqrt(q.value())));
    }

    template <class R, detail::dim D>
    [[nodiscard]] constexpr bool is_dimless(const Quantity<R, D>&) noexcept {
        return detail::is_dimless_v<D>;
    }

    template <class R, detail::dim D>
    [[nodiscard]] constexpr auto as_dimless(const Quantity<R, D>& q)
        requires detail::is_dimless_v<D>
    {
        return q.value();
    }

    namespace literals {

        [[nodiscard]] constexpr Quantity<long double, Length> operator"" _m(long double v) {
            return Quantity<long double, Length>(v);
        }

        [[nodiscard]] constexpr Quantity<long double, Time> operator"" _s(long double v) {
            return Quantity<long double, Time>(v);
        }

        [[nodiscard]] constexpr Quantity<long double, Mass> operator"" _kg(long double v) {
            return Quantity<long double, Mass>(v);
        }

    }  // namespace literals

}  // namespace mathfp::units

template <class Rep, class D, class Char>
struct fmt::formatter<mathfp::units::Quantity<Rep, D>, Char> : fmt::formatter<Rep, Char> {
    template <class FormatContext>
    auto format(const mathfp::units::Quantity<Rep, D>& q, FormatContext& ctx) const {
        return fmt::formatter<Rep, Char>::format(q.value(), ctx);
    }
};
