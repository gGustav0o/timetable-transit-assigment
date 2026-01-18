#pragma once

#include <concepts>
#include <compare>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <mathfp/compiler_attributes.hpp>

namespace mathfp {

    namespace strong_detail {

        template <class Derived>
        struct EqualityComparable {
            friend constexpr bool operator==(const Derived& a, const Derived& b)
                requires requires { a.get() == b.get(); }
            {
                return a.get() == b.get();
            }
        };

        template <class Derived>
        struct Ordered {
            friend constexpr auto operator<=>(const Derived& a, const Derived& b)
                requires requires { a.get() <=> b.get(); }
            {
                return a.get() <=> b.get();
            }
        };

        template <class Derived>
        struct Additive {
            friend constexpr Derived operator+(const Derived& a, const Derived& b)
                requires requires { a.get() + b.get(); }
            {
                return Derived(a.get() + b.get());
            }

            friend constexpr Derived operator-(const Derived& a, const Derived& b)
                requires requires { a.get() - b.get(); }
            {
                return Derived(a.get() - b.get());
            }

            friend constexpr Derived& operator+=(Derived& a, const Derived& b)
                requires requires { a.get() += b.get(); }
            {
                a.get_mut() += b.get();
                return a;
            }

            friend constexpr Derived& operator-=(Derived& a, const Derived& b)
                requires requires { a.get() -= b.get(); }
            {
                a.get_mut() -= b.get();
                return a;
            }
        };

        template <class Derived>
        struct ScalarMultipliable {
            template <class S>
            friend constexpr Derived operator*(const Derived& x, const S& s)
                requires requires { x.get()* s; }
            {
                return Derived(x.get() * s);
            }

            template <class S>
            friend constexpr Derived operator*(const S& s, const Derived& x)
                requires requires { s* x.get(); }
            {
                return Derived(s * x.get());
            }

            template <class S>
            friend constexpr Derived& operator*=(Derived& x, const S& s)
                requires requires { x.get() *= s; }
            {
                x.get_mut() *= s;
                return x;
            }
        };

        template <class Derived>
        struct Incrementable {
            friend constexpr Derived& operator++(Derived& x)
                requires requires { ++x.get_mut(); }
            {
                ++x.get_mut();
                return x;
            }

            friend constexpr Derived operator++(Derived& x, int)
                requires requires { x.get(); ++x.get_mut(); }
            {
                auto old = x;
                ++x;
                return old;
            }
        };

    }  // namespace strong_detail

    template <class T, class Tag, template <class> class... Skills>
    class StrongType : public Skills<StrongType<T, Tag, Skills...>>... {
    public:
        using ValueType = T;
        using TagType = Tag;

        constexpr StrongType() = default;

        constexpr explicit StrongType(T v) noexcept(std::is_nothrow_move_constructible_v<T>)
            : value_(std::move(v)) {
        }

        MATHFP_NODISCARD constexpr const T& get() const& noexcept { return value_; }
        MATHFP_NODISCARD constexpr T& get_mut() & noexcept { return value_; }
        MATHFP_NODISCARD constexpr T&& get() && noexcept { return std::move(value_); }

        MATHFP_NODISCARD constexpr const T& unwrap() const& noexcept { return value_; }
        MATHFP_NODISCARD constexpr T unwrap() && noexcept(std::is_nothrow_move_constructible_v<T>) {
            return std::move(value_);
        }

    private:
        T value_{};
    };


    template <class T>
    struct is_strong_type : std::false_type {};

    template <class T, class Tag, template <class> class... Skills>
    struct is_strong_type<StrongType<T, Tag, Skills...>> : std::true_type {};

    template <class T>
    inline constexpr bool is_strong_type_v = is_strong_type<std::remove_cvref_t<T>>::value;

    template <class T, class Tag, template <class> class... Skills, class F>
    MATHFP_NODISCARD constexpr auto map_strong(const StrongType<T, Tag, Skills...>& x, F&& f)
        -> StrongType<std::invoke_result_t<F, const T&>, Tag, Skills...>
        requires requires { std::invoke(std::forward<F>(f), x.get()); }
    {
        using U = std::invoke_result_t<F, const T&>;
        return StrongType<U, Tag, Skills...>(std::invoke(std::forward<F>(f), x.get()));
    }

}  // namespace mathfp

template <class T, class Tag, template <class> class... Skills, class Char>
struct fmt::formatter<mathfp::StrongType<T, Tag, Skills...>, Char> {
    fmt::formatter<T, Char> inner;

    constexpr auto parse(fmt::basic_format_parse_context<Char>& ctx) {
        return inner.parse(ctx);
    }

    template <class FormatContext>
    auto format(const mathfp::StrongType<T, Tag, Skills...>& v, FormatContext& ctx) const {
        return inner.format(v.get(), ctx);
    }
};

namespace std {
    template <class T, class Tag, template <class> class... Skills>
    struct hash<mathfp::StrongType<T, Tag, Skills...>> {
        std::size_t operator()(const mathfp::StrongType<T, Tag, Skills...>& v) const noexcept {
            return std::hash<T>{}(v.get());
        }
    };
}  // namespace std
