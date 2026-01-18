#pragma once

#include <concepts>
#include <cstddef>
#include <limits>
#include <source_location>
#include <type_traits>
#include <utility>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/types/strong_type.hpp>

namespace mathfp {

    template <class Tag>
    using Index = StrongType<
        std::size_t
        , Tag
        , strong_detail::EqualityComparable
        , strong_detail::Ordered
        , strong_detail::Incrementable
    >;

    template <class Tag>
    inline constexpr std::size_t kInvalidIndexValue = std::numeric_limits<std::size_t>::max();

    template <class Tag>
    MATHFP_NODISCARD constexpr Index<Tag> invalid_index() noexcept {
        return Index<Tag>(kInvalidIndexValue<Tag>);
    }

    template <class Tag>
    MATHFP_NODISCARD constexpr bool is_valid(Index<Tag> i) noexcept {
        return i.get() != kInvalidIndexValue<Tag>;
    }

    template <class Tag>
    MATHFP_NODISCARD constexpr std::size_t to_usize(Index<Tag> i) noexcept {
        return i.get();
    }

    template <class Tag, std::integral I>
    MATHFP_NODISCARD inline Expected<Index<Tag>> make_index(
        I i
        , std::source_location where = std::source_location::current()
    ) {
        if constexpr (std::is_signed_v<I>) {
            if (i < 0) {
                return unexpected(
                    invalid_arg("index must be non-negative", where)
                    .ctx("index", i));
            }
        }
        const auto v = static_cast<std::size_t>(i);
        if (v == kInvalidIndexValue<Tag>) {
            return unexpected(
                invalid_arg("index value is reserved (invalid sentinel)", where)
                .ctx("index", v));
        }
        return Index<Tag>(v);
    }

    template <class Tag>
    MATHFP_NODISCARD inline Expected<Index<Tag>> next_index(
        Index<Tag> i,
        std::source_location where = std::source_location::current()) {
        if (!is_valid<Tag>(i)) {
            return unexpected(domain_error("cannot advance invalid index", where));
        }
        if (i.get() == kInvalidIndexValue<Tag> -1) {
            return unexpected(overflow_error("index overflow (reached sentinel boundary)", where)
                .ctx("index", i.get()));
        }
        return Index<Tag>(i.get() + 1);
    }

    template <class Tag>
    MATHFP_NODISCARD inline Expected<Index<Tag>> prev_index(
        Index<Tag> i,
        std::source_location where = std::source_location::current()) {
        if (!is_valid<Tag>(i)) {
            return unexpected(domain_error("cannot decrement invalid index", where));
        }
        if (i.get() == 0) {
            return unexpected(domain_error("index underflow (already 0)", where)
                .ctx("index", i.get()));
        }
        return Index<Tag>(i.get() - 1);
    }

}  // namespace mathfp
