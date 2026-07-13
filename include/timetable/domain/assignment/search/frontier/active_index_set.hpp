#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace timetable::domain::assignment {

    struct ActiveIndexSet final {
        static constexpr std::size_t word_bits = 64;
        static constexpr std::size_t inline_word_count = 4;

        std::size_t size{};
        std::array<std::uint64_t, inline_word_count> inline_words{};
        std::vector<std::uint64_t> heap_words{};

        ActiveIndexSet() = default;

        explicit ActiveIndexSet(std::size_t element_count)
            : size{ element_count }
        {
            if (!uses_inline_storage()) {
                heap_words.assign(word_count(), 0u);
            }
        }

        [[nodiscard]] static ActiveIndexSet full(std::size_t element_count) {
            ActiveIndexSet result{ element_count };
            for (std::size_t i = 0; i < result.word_count(); ++i) {
                result.word(i) = ~std::uint64_t{ 0 };
            }
            const auto tail_bits = element_count % word_bits;
            if (result.word_count() != 0u && tail_bits != 0u) {
                result.word(result.word_count() - 1u) &=
                    (std::uint64_t{ 1 } << tail_bits) - 1u;
            }
            return result;
        }

        [[nodiscard]] std::size_t word_count() const noexcept {
            return (size + word_bits - 1u) / word_bits;
        }

        [[nodiscard]] bool uses_inline_storage() const noexcept {
            return word_count() <= inline_word_count;
        }

        [[nodiscard]] std::uint64_t word(std::size_t index) const noexcept {
            return uses_inline_storage()
                ? inline_words[index]
                : heap_words[index];
        }

        [[nodiscard]] std::uint64_t& word(std::size_t index) noexcept {
            return uses_inline_storage()
                ? inline_words[index]
                : heap_words[index];
        }

        [[nodiscard]] bool empty() const noexcept {
            for (std::size_t i = 0; i < word_count(); ++i) {
                if (word(i) != 0u) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] std::size_t active_count() const noexcept {
            std::size_t count = 0;
            for (std::size_t i = 0; i < word_count(); ++i) {
                count += static_cast<std::size_t>(std::popcount(word(i)));
            }
            return count;
        }

        [[nodiscard]] bool contains(std::size_t index) const noexcept {
            return index < size
                && (word(index / word_bits)
                    & (std::uint64_t{ 1 } << (index % word_bits))) != 0u;
        }

        void set(std::size_t index) noexcept {
            if (index >= size) {
                return;
            }
            word(index / word_bits) |= std::uint64_t{ 1 } << (index % word_bits);
        }

        [[nodiscard]] ActiveIndexSet intersect(
            const ActiveIndexSet& rhs
        ) const {
            ActiveIndexSet result{ size < rhs.size ? size : rhs.size };
            const auto common_words =
                word_count() < rhs.word_count() ? word_count() : rhs.word_count();
            for (std::size_t i = 0; i < common_words; ++i) {
                result.word(i) = word(i) & rhs.word(i);
            }
            return result;
        }

        [[nodiscard]] bool equals(const ActiveIndexSet& rhs) const noexcept {
            if (size != rhs.size) {
                return false;
            }
            for (std::size_t i = 0; i < word_count(); ++i) {
                if (word(i) != rhs.word(i)) {
                    return false;
                }
            }
            return true;
        }

        template <typename Visitor>
        void for_each_index(Visitor&& visit) const {
            auto&& visitor = visit;
            for (std::size_t word_index = 0; word_index < word_count(); ++word_index) {
                auto bits = word(word_index);
                while (bits != 0u) {
                    const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
                    const auto index = word_index * word_bits + bit;
                    if (index < size) {
                        visitor(index);
                    }
                    bits &= bits - 1u;
                }
            }
        }

        template <typename Visitor>
        void for_each_difference_index(
              const ActiveIndexSet& rhs
            , Visitor&&             visit
        ) const {
            auto&& visitor = visit;
            const auto lhs_words = word_count();
            const auto rhs_words = rhs.word_count();
            for (std::size_t word_index = 0; word_index < lhs_words; ++word_index) {
                const auto rhs_word = word_index < rhs_words
                    ? rhs.word(word_index)
                    : std::uint64_t{ 0 };
                auto bits = word(word_index) & ~rhs_word;
                while (bits != 0u) {
                    const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
                    const auto index = word_index * word_bits + bit;
                    if (index < size) {
                        visitor(index);
                    }
                    bits &= bits - 1u;
                }
            }
        }
    };

    struct FixedActiveMask final {
        static constexpr std::size_t max_words = ActiveIndexSet::inline_word_count;
        static constexpr std::size_t max_size  = max_words * ActiveIndexSet::word_bits;

        std::size_t size{};
        std::array<std::uint64_t, max_words> words{};

        [[nodiscard]] static FixedActiveMask from(
            const ActiveIndexSet& source
        ) noexcept {
            FixedActiveMask result{
                  .size = source.size
            };
            const auto copied_words =
                source.word_count() < max_words ? source.word_count() : max_words;
            for (std::size_t i = 0; i < copied_words; ++i) {
                result.words[i] = source.word(i);
            }
            return result;
        }

        [[nodiscard]] ActiveIndexSet to_active_index_set() const {
            ActiveIndexSet result{ size };
            const auto copied_words =
                result.word_count() < max_words ? result.word_count() : max_words;
            for (std::size_t i = 0; i < copied_words; ++i) {
                result.word(i) = words[i];
            }
            return result;
        }
    };

    struct DemandBranchProjectionState final {
        ActiveIndexSet active_tasks{};
        ActiveIndexSet active_targets{};
    };

}  // namespace timetable::domain::assignment
