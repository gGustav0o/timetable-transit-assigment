#pragma once

#include <cstddef>
#include <iterator>
#include <tuple>
#include <type_traits>
#include <utility>

#include <mathfp/compiler_attributes.hpp>

namespace mathfp::ranges {
    namespace detail {

        template <class TupleA, class TupleB, std::size_t... Is>
        MATHFP_NODISCARD constexpr bool any_iterator_at_end_impl(
            const TupleA& current
            , const TupleB& end
            , std::index_sequence<Is...>
        ) {
            return ((std::get<Is>(current) == std::get<Is>(end)) || ...);
        }

        template <class TupleA, class TupleB>
        MATHFP_NODISCARD constexpr bool any_iterator_at_end(
            const TupleA& current
            , const TupleB& end
        ) {
            return any_iterator_at_end_impl(
                current
                , end
                , std::make_index_sequence<std::tuple_size_v<std::remove_reference_t<TupleA>>>{}
            );
        }

        template <class Tuple>
        MATHFP_NODISCARD constexpr auto dereference_tuple(const Tuple& iterators) {
            return std::apply(
                [](const auto&... it) {
                    return std::tuple<decltype(*it)...>{ *it... };
                }
                , iterators
            );
        }

    }  // namespace detail

    template <class... Rs>
    class ZipRange final {
        using RangeTuple = std::tuple<Rs*...>;
        using IteratorTuple = std::tuple<decltype(std::begin(std::declval<Rs&>()))...>;
        using SentinelTuple = std::tuple<decltype(std::end(std::declval<Rs&>()))...>;

    public:
        explicit ZipRange(Rs&... ranges)
            : ranges_{ &ranges... } {
        }

        class iterator final {
        public:
            using difference_type = std::ptrdiff_t;

            iterator(
                IteratorTuple current
                , SentinelTuple end
                , bool done
            )
                : current_(std::move(current))
                , end_(std::move(end))
                , done_(done) {
            }

            iterator& operator++() {
                std::apply(
                    [](auto&... it) {
                        (++it, ...);
                    }
                    , current_
                );
                done_ = detail::any_iterator_at_end(current_, end_);
                return *this;
            }

            void operator++(int) {
                ++(*this);
            }

            MATHFP_NODISCARD bool operator==(const iterator& other) const {
                if (done_ && other.done_) {
                    return true;
                }
                return current_ == other.current_;
            }

            MATHFP_NODISCARD bool operator!=(const iterator& other) const {
                return !(*this == other);
            }

            MATHFP_NODISCARD auto operator*() const {
                return detail::dereference_tuple(current_);
            }

        private:
            IteratorTuple current_;
            SentinelTuple end_;
            bool done_{ true };
        };

        MATHFP_NODISCARD iterator begin() {
            auto end = make_end_tuple();
            auto begin = make_begin_tuple();
            const auto done = detail::any_iterator_at_end(begin, end);
            return iterator{
                std::move(begin)
                , end
                , done
            };
        }

        MATHFP_NODISCARD iterator end() {
            auto end = make_end_tuple();
            return iterator{ end, end, true };
        }

        MATHFP_NODISCARD iterator begin() const {
            auto end = make_end_tuple();
            auto begin = make_begin_tuple();
            const auto done = detail::any_iterator_at_end(begin, end);
            return iterator{
                std::move(begin)
                , end
                , done
            };
        }

        MATHFP_NODISCARD iterator end() const {
            auto end = make_end_tuple();
            return iterator{ end, end, true };
        }

    private:
        MATHFP_NODISCARD IteratorTuple make_begin_tuple() const {
            return std::apply(
                [](auto*... ranges) {
                    return IteratorTuple{ std::begin(*ranges)... };
                }
                , ranges_
            );
        }

        MATHFP_NODISCARD SentinelTuple make_end_tuple() const {
            return std::apply(
                [](auto*... ranges) {
                    return SentinelTuple{ std::end(*ranges)... };
                }
                , ranges_
            );
        }

        RangeTuple ranges_{};
    };

    template <class... Rs>
    MATHFP_NODISCARD inline auto zip(Rs&... rs) {
        return ZipRange<Rs...>(rs...);
    }

    template <class F, class... Rs>
    class ZipWithRange final {
        using Function = std::decay_t<F>;
        using ZippedRange = ZipRange<Rs...>;

    public:
        ZipWithRange(F&& function, Rs&... ranges)
            : zipped_(ranges...)
            , fn_(std::forward<F>(function)) {
        }

        class iterator final {
            using BaseIterator = typename ZippedRange::iterator;

        public:
            iterator(
                BaseIterator current
                , Function* fn
            )
                : current_(std::move(current))
                , fn_(fn) {
            }

            iterator& operator++() {
                ++current_;
                return *this;
            }

            void operator++(int) {
                ++(*this);
            }

            MATHFP_NODISCARD bool operator==(const iterator& other) const {
                return current_ == other.current_;
            }

            MATHFP_NODISCARD bool operator!=(const iterator& other) const {
                return !(*this == other);
            }

            MATHFP_NODISCARD decltype(auto) operator*() const {
                auto tuple = *current_;
                return std::apply(*fn_, tuple);
            }

        private:
            BaseIterator current_;
            Function* fn_{ nullptr };
        };

        MATHFP_NODISCARD iterator begin() {
            return iterator{ zipped_.begin(), &fn_ };
        }

        MATHFP_NODISCARD iterator end() {
            return iterator{ zipped_.end(), &fn_ };
        }

        MATHFP_NODISCARD iterator begin() const {
            return iterator{ zipped_.begin(), const_cast<Function*>(&fn_) };
        }

        MATHFP_NODISCARD iterator end() const {
            return iterator{ zipped_.end(), const_cast<Function*>(&fn_) };
        }

    private:
        ZippedRange zipped_;
        Function fn_;
    };

    template <class F, class... Rs>
    MATHFP_NODISCARD inline auto zip_with(F&& f, Rs&... rs) {
        return ZipWithRange<F, Rs...>(std::forward<F>(f), rs...);
    }

}  // namespace mathfp::ranges
