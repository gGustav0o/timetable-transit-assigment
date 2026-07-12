#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <functional>
#include <limits>
#include <source_location>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/core/checked_arithmetic.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/types/strong_type.hpp>

namespace mathfp {

    template <class Tag>
    class Index;

    template <class T>
    struct is_index : std::false_type {};

    template <class T>
    inline constexpr bool is_index_v =
        is_index<std::remove_cvref_t<T>>::value;

    namespace index_detail {

        template <class I>
        concept IndexInput =
               std::integral<std::remove_cvref_t<I>>
            && !std::same_as<std::remove_cvref_t<I>, bool>
            && !strong_detail::CharacterType<I>;

        template <class Tag>
        MATHFP_NODISCARD constexpr Index<Tag> make_unchecked_index(
            std::size_t value
        ) noexcept;

        template <class Target, class Source>
        MATHFP_NODISCARD constexpr bool exceeds_max(Source value) noexcept {
            using RawSource = std::remove_cvref_t<Source>;
            using UnsignedSource = std::make_unsigned_t<RawSource>;

            return static_cast<UnsignedSource>(value)
                > static_cast<UnsignedSource>(std::numeric_limits<Target>::max());
        }

    }  // namespace index_detail

    template <class Tag>
    class Index final {
    public:
        using ValueType = std::size_t;
        using TagType   = Tag;

        static constexpr bool kHashEnabled = true;

        Index() = delete;

        MATHFP_NODISCARD constexpr const std::size_t& get() const& noexcept {
            return value_;
        }

        MATHFP_NODISCARD constexpr std::size_t get() && noexcept {
            return value_;
        }

        MATHFP_NODISCARD constexpr const std::size_t& unwrap() const& noexcept {
            return value_;
        }

        MATHFP_NODISCARD constexpr std::size_t unwrap() && noexcept {
            return value_;
        }

        MATHFP_NODISCARD friend constexpr bool operator==(
              Index lhs
            , Index rhs
        ) noexcept {
            return lhs.value_ == rhs.value_;
        }

        MATHFP_NODISCARD friend constexpr auto operator<=>(
              Index lhs
            , Index rhs
        ) noexcept {
            return lhs.value_ <=> rhs.value_;
        }

    private:
        struct unchecked_t {};

        constexpr explicit Index(std::size_t value, unchecked_t) noexcept
            : value_(value) {
        }

        template <class U>
        friend constexpr Index<U> index_detail::make_unchecked_index(
            std::size_t value
        ) noexcept;

        std::size_t value_;
    };

    template <class Tag>
    struct is_strong_type<Index<Tag>> : std::true_type {};

    template <class Tag>
    struct is_index<Index<Tag>> : std::true_type {};

    namespace index_detail {

        template <class Tag>
        MATHFP_NODISCARD constexpr Index<Tag> make_unchecked_index(
            std::size_t value
        ) noexcept {
            return Index<Tag>(value, typename Index<Tag>::unchecked_t{});
        }

    }  // namespace index_detail

    template <class Tag>
    MATHFP_NODISCARD constexpr std::size_t to_usize(Index<Tag> i) noexcept {
        return i.get();
    }

    template <class Tag, index_detail::IndexInput I>
    MATHFP_NODISCARD inline Expected<Index<Tag>> make_index(
          I                   i
        , std::source_location where = std::source_location::current()
    ) {
        using Raw = std::remove_cvref_t<I>;

        if constexpr (std::is_signed_v<Raw>) {
            if (i < 0) {
                return unexpected(
                    invalid_arg("index must be non-negative", where)
                    .ctx("index", i));
            }
        }
        if constexpr (sizeof(Raw) > sizeof(std::size_t)) {
            if (index_detail::exceeds_max<std::size_t>(i)) {
                return unexpected(
                    overflow_error("index value exceeds size_t", where)
                    .ctx("size_t_max", std::numeric_limits<std::size_t>::max()));
            }
        }
        const auto v = static_cast<std::size_t>(i);
        return index_detail::make_unchecked_index<Tag>(v);
    }

    template <class Tag>
    MATHFP_NODISCARD inline Expected<Index<Tag>> next_index(
        Index<Tag> i,
        std::source_location where = std::source_location::current()) {
        const auto next = checked_add(i.get(), std::size_t{1}, where);
        if (!next) {
            return unexpected(next.error());
        }
        return index_detail::make_unchecked_index<Tag>(*next);
    }

    template <class Tag>
    MATHFP_NODISCARD inline Expected<Index<Tag>> prev_index(
        Index<Tag> i,
        std::source_location where = std::source_location::current()) {
        const auto prev = checked_sub(i.get(), std::size_t{1}, where);
        if (!prev) {
            return unexpected(prev.error());
        }
        return index_detail::make_unchecked_index<Tag>(*prev);
    }

}  // namespace mathfp

template <class Tag, class Char>
struct fmt::formatter<mathfp::Index<Tag>, Char> {
    fmt::formatter<std::size_t, Char> inner;

    constexpr auto parse(fmt::basic_format_parse_context<Char>& ctx) {
        return inner.parse(ctx);
    }

    template <class FormatContext>
    auto format(
          mathfp::Index<Tag> value
        , FormatContext&     ctx
    ) const {
        return inner.format(value.get(), ctx);
    }
};

namespace std {
    template <class Tag>
    struct hash<mathfp::Index<Tag>> {
        std::size_t operator()(mathfp::Index<Tag> value) const noexcept {
            return std::hash<std::size_t>{}(value.get());
        }
    };
}  // namespace std
