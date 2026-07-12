#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <ostream>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <mathfp/compiler_attributes.hpp>

namespace mathfp {

    namespace strong_detail {

        template <class T>
        using remove_cvref_t = std::remove_cvref_t<T>;

        template <class T>
        concept EqualityResult = std::convertible_to<T, bool>;

        template <class T>
        concept ComparisonCategory =
               std::same_as<remove_cvref_t<T>, std::strong_ordering>
            || std::same_as<remove_cvref_t<T>, std::weak_ordering>
            || std::same_as<remove_cvref_t<T>, std::partial_ordering>;

        template <class T>
        concept CharacterType =
               std::same_as<remove_cvref_t<T>, char>
            || std::same_as<remove_cvref_t<T>, wchar_t>
            || std::same_as<remove_cvref_t<T>, char8_t>
            || std::same_as<remove_cvref_t<T>, char16_t>
            || std::same_as<remove_cvref_t<T>, char32_t>;

        template <class T>
        concept IntegralValue =
               std::integral<remove_cvref_t<T>>
            && !std::same_as<remove_cvref_t<T>, bool>
            && !CharacterType<T>;

        template <class T>
        concept ArithmeticScalar =
               std::is_arithmetic_v<remove_cvref_t<T>>
            && !std::same_as<remove_cvref_t<T>, bool>
            && !CharacterType<T>;

        template <class T>
        concept StrongValueType =
               std::same_as<T, std::remove_cv_t<T>>
            && !std::is_reference_v<T>
            && !std::is_array_v<T>
            && !std::is_function_v<T>
            && std::is_object_v<T>
            && std::move_constructible<T>;

        template <class T>
        concept ValueEqualityComparable = requires(const T& a, const T& b) {
            { a == b } -> EqualityResult;
        };

        template <class T>
        concept ValueOrdered = requires(const T& a, const T& b) {
            { a <=> b } -> ComparisonCategory;
        };

        template <class Derived>
        concept EqualityComparableValue =
            ValueEqualityComparable<typename Derived::ValueType>;

        template <class Derived>
        concept OrderedValue =
            ValueOrdered<typename Derived::ValueType>;

        template <class Target, class Source>
        concept ExactlyMaterializable =
               StrongValueType<Target>
            && std::same_as<remove_cvref_t<Source>, Target>
            && std::constructible_from<Target, Source>;

        template <class Target, class Source>
            requires ExactlyMaterializable<Target, Source&&>
        MATHFP_NODISCARD constexpr Target materialize_value(Source&& value)
            noexcept(std::is_nothrow_constructible_v<Target, Source&&>)
        {
            return Target(std::forward<Source>(value));
        }

        template <class Derived, class R>
        concept MaterializableValueResult =
               ExactlyMaterializable<typename Derived::ValueType, R>
            && std::constructible_from<Derived, typename Derived::ValueType>;

        template <class Derived>
        concept AddConstructible =
               !IntegralValue<typename Derived::ValueType>
            && requires(const Derived& a, const Derived& b) {
                requires MaterializableValueResult<Derived, decltype(a.get() + b.get())>;
            };

        template <class Derived>
        concept SubtractConstructible =
               !IntegralValue<typename Derived::ValueType>
            && requires(const Derived& a, const Derived& b) {
                requires MaterializableValueResult<Derived, decltype(a.get() - b.get())>;
            };

        template <class Derived, class Scalar>
        concept RightScalarMultiplyConstructible =
               !IntegralValue<typename Derived::ValueType>
            && ArithmeticScalar<Scalar>
            && requires(const Derived& x, const Scalar& scalar) {
                requires MaterializableValueResult<Derived, decltype(x.get() * scalar)>;
            };

        template <class Derived, class Scalar>
        concept LeftScalarMultiplyConstructible =
               !IntegralValue<typename Derived::ValueType>
            && ArithmeticScalar<Scalar>
            && requires(const Scalar& scalar, const Derived& x) {
                requires MaterializableValueResult<Derived, decltype(scalar * x.get())>;
            };

        template <class Derived>
        concept AssignableFromSelf = std::assignable_from<Derived&, Derived>;

        template <class Derived>
        MATHFP_NODISCARD constexpr Derived add_values(
              const Derived& a
            , const Derived& b
        )
            noexcept(noexcept(Derived(
                materialize_value<typename Derived::ValueType>(a.get() + b.get())
            )))
        {
            using Value = typename Derived::ValueType;
            return Derived(materialize_value<Value>(a.get() + b.get()));
        }

        template <class Derived>
        MATHFP_NODISCARD constexpr Derived subtract_values(
              const Derived& a
            , const Derived& b
        )
            noexcept(noexcept(Derived(
                materialize_value<typename Derived::ValueType>(a.get() - b.get())
            )))
        {
            using Value = typename Derived::ValueType;
            return Derived(materialize_value<Value>(a.get() - b.get()));
        }

        template <class Derived, class Scalar>
        MATHFP_NODISCARD constexpr Derived multiply_right(
              const Derived& x
            , const Scalar& scalar
        )
            noexcept(noexcept(Derived(
                materialize_value<typename Derived::ValueType>(x.get() * scalar)
            )))
        {
            using Value = typename Derived::ValueType;
            return Derived(materialize_value<Value>(x.get() * scalar));
        }

        template <class Scalar, class Derived>
        MATHFP_NODISCARD constexpr Derived multiply_left(
              const Scalar& scalar
            , const Derived& x
        )
            noexcept(noexcept(Derived(
                materialize_value<typename Derived::ValueType>(scalar * x.get())
            )))
        {
            using Value = typename Derived::ValueType;
            return Derived(materialize_value<Value>(scalar * x.get()));
        }

        template <class T>
        concept StdHashable = requires(const T& value) {
            { std::hash<T>{}(value) } -> std::convertible_to<std::size_t>;
        };

        template <template <class> class A, template <class> class B>
        inline constexpr bool same_skill_v = false;

        template <template <class> class A>
        inline constexpr bool same_skill_v<A, A> = true;

        template <template <class> class... Skills>
        struct unique_skills : std::true_type {};

        template <template <class> class First, template <class> class... Rest>
        struct unique_skills<First, Rest...>
            : std::bool_constant<
                  ((!same_skill_v<First, Rest>) && ...)
               && unique_skills<Rest...>::value
              > {};

        template <template <class> class... Skills>
        concept UniqueSkills = unique_skills<Skills...>::value;

        template <class T>
        MATHFP_NODISCARD constexpr decltype(auto) stream_materialized_value(
            const T& value
        ) noexcept {
            if constexpr (std::same_as<remove_cvref_t<T>, signed char>) {
                return static_cast<int>(value);
            }
            else if constexpr (std::same_as<remove_cvref_t<T>, unsigned char>) {
                return static_cast<unsigned int>(value);
            }
            else {
                return (value);
            }
        }

        template <class Derived>
        struct EqualityComparable {
            MATHFP_NODISCARD friend constexpr bool operator==(
                  const Derived& a
                , const Derived& b
            )
                noexcept(noexcept(static_cast<bool>(a.get() == b.get())))
                requires EqualityComparableValue<Derived>
            {
                return static_cast<bool>(a.get() == b.get());
            }
        };

        template <class Derived>
        struct Ordered {
            MATHFP_NODISCARD friend constexpr auto operator<=>(
                  const Derived& a
                , const Derived& b
            )
                noexcept(noexcept(a.get() <=> b.get()))
                requires OrderedValue<Derived>
            {
                return a.get() <=> b.get();
            }
        };

        template <class Derived>
        struct Additive {
            MATHFP_NODISCARD friend constexpr Derived operator+(
                  const Derived& a
                , const Derived& b
            )
                requires AddConstructible<Derived>
            {
                return add_values(a, b);
            }

            MATHFP_NODISCARD friend constexpr Derived operator-(
                  const Derived& a
                , const Derived& b
            )
                requires SubtractConstructible<Derived>
            {
                return subtract_values(a, b);
            }

            friend constexpr Derived& operator+=(Derived& a, const Derived& b)
                requires AddConstructible<Derived> && AssignableFromSelf<Derived>
            {
                a = add_values(a, b);
                return a;
            }

            friend constexpr Derived& operator-=(Derived& a, const Derived& b)
                requires SubtractConstructible<Derived> && AssignableFromSelf<Derived>
            {
                a = subtract_values(a, b);
                return a;
            }
        };

        template <class Derived>
        struct ScalarMultipliable {
            template <class Scalar>
            MATHFP_NODISCARD friend constexpr Derived operator*(
                  const Derived& x
                , const Scalar& scalar
            )
                requires RightScalarMultiplyConstructible<Derived, Scalar>
            {
                return multiply_right(x, scalar);
            }

            template <class Scalar>
            MATHFP_NODISCARD friend constexpr Derived operator*(
                  const Scalar& scalar
                , const Derived& x
            )
                requires LeftScalarMultiplyConstructible<Derived, Scalar>
            {
                return multiply_left(scalar, x);
            }

            template <class Scalar>
            friend constexpr Derived& operator*=(Derived& x, const Scalar& scalar)
                requires RightScalarMultiplyConstructible<Derived, Scalar>
                      && AssignableFromSelf<Derived>
            {
                x = multiply_right(x, scalar);
                return x;
            }
        };

        template <class Derived>
        struct Streamable {
            template <class CharT, class Traits>
            friend std::basic_ostream<CharT, Traits>& operator<<(
                  std::basic_ostream<CharT, Traits>& os
                , const Derived& value
            )
                noexcept(noexcept(os << stream_materialized_value(value.get())))
                requires requires(
                      std::basic_ostream<CharT, Traits>& stream
                    , const typename Derived::ValueType& raw
                ) {
                    { stream << stream_materialized_value(raw) }
                        -> std::same_as<std::basic_ostream<CharT, Traits>&>;
                }
            {
                return os << stream_materialized_value(value.get());
            }
        };

        template <class Derived>
        struct Hashable {};

        template <class Derived, bool Enabled>
        struct ordered_equality_base {};

        template <class Derived>
        struct ordered_equality_base<Derived, true>
            : EqualityComparable<Derived> {};

    }  // namespace strong_detail

    template <class T, class Tag, template <class> class... Skills>
        requires strong_detail::StrongValueType<T>
              && strong_detail::UniqueSkills<Skills...>
    class StrongType final
        : private strong_detail::ordered_equality_base<
              StrongType<T, Tag, Skills...>
            , (strong_detail::same_skill_v<strong_detail::Ordered, Skills> || ...)
              && strong_detail::ValueEqualityComparable<T>
              && !(strong_detail::same_skill_v<strong_detail::EqualityComparable, Skills> || ...)
          >
        , public Skills<StrongType<T, Tag, Skills...>>... {
    public:
        using ValueType = T;
        using TagType   = Tag;

        static constexpr bool kEqualityEnabled =
               (strong_detail::same_skill_v<strong_detail::EqualityComparable, Skills> || ...)
            || (
                   (strong_detail::same_skill_v<strong_detail::Ordered, Skills> || ...)
                && strong_detail::ValueEqualityComparable<T>
               );

        static constexpr bool kHashEnabled =
            (strong_detail::same_skill_v<strong_detail::Hashable, Skills> || ...);

        static_assert(
              !kHashEnabled || kEqualityEnabled
            , "mathfp::strong_detail::Hashable requires equality-compatible strong type"
        );

        static_assert(
              !kHashEnabled || strong_detail::StdHashable<T>
            , "mathfp::strong_detail::Hashable requires std::hash<ValueType>"
        );

        StrongType() = delete;

        constexpr explicit StrongType(T value)
            noexcept(std::is_nothrow_move_constructible_v<T>)
            : value_(std::move(value)) {
        }

        MATHFP_NODISCARD constexpr const T& get() const& noexcept {
            return value_;
        }

        MATHFP_NODISCARD constexpr const T& unwrap() const& noexcept {
            return value_;
        }

        MATHFP_NODISCARD constexpr T unwrap() &&
            noexcept(std::is_nothrow_move_constructible_v<T>) {
            return std::move(value_);
        }

    private:
        T value_;
    };

    template <class T>
    struct is_strong_type : std::false_type {};

    template <class T, class Tag, template <class> class... Skills>
    struct is_strong_type<StrongType<T, Tag, Skills...>> : std::true_type {};

    template <class T>
    inline constexpr bool is_strong_type_v =
        is_strong_type<std::remove_cvref_t<T>>::value;

    template <class T, class Tag, template <class> class... Skills, class F>
    MATHFP_NODISCARD constexpr auto map_strong(
          const StrongType<T, Tag, Skills...>& x
        , F&&                                  f
    )
        -> StrongType<std::invoke_result_t<F, const T&>, Tag, Skills...>
        requires requires { std::invoke(std::forward<F>(f), x.get()); }
    {
        using U = std::invoke_result_t<F, const T&>;
        return StrongType<U, Tag, Skills...>(
            std::invoke(std::forward<F>(f), x.get())
        );
    }

}  // namespace mathfp

template <class T, class Tag, template <class> class... Skills, class Char>
struct fmt::formatter<mathfp::StrongType<T, Tag, Skills...>, Char> {
    fmt::formatter<T, Char> inner;

    constexpr auto parse(fmt::basic_format_parse_context<Char>& ctx) {
        return inner.parse(ctx);
    }

    template <class FormatContext>
    auto format(
          const mathfp::StrongType<T, Tag, Skills...>& value
        , FormatContext&                               ctx
    ) const {
        return inner.format(value.get(), ctx);
    }
};

namespace std {
    template <class T, class Tag, template <class> class... Skills>
        requires (
               mathfp::StrongType<T, Tag, Skills...>::kHashEnabled
            && mathfp::strong_detail::StdHashable<T>
        )
    struct hash<mathfp::StrongType<T, Tag, Skills...>> {
        std::size_t operator()(
            const mathfp::StrongType<T, Tag, Skills...>& value
        ) const noexcept(noexcept(std::hash<T>{}(value.get()))) {
            return std::hash<T>{}(value.get());
        }
    };
}  // namespace std
