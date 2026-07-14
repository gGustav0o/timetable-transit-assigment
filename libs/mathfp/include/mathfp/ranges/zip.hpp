#pragma once

#include <cstddef>
#include <functional>
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

        template <class F, class Tuple>
        struct tuple_invocable : std::false_type {};

        template <class F, class... Ts>
        struct tuple_invocable<F, std::tuple<Ts...>>
            : std::bool_constant<std::is_invocable_v<F, Ts...>> {};

        template <class F, class Tuple>
        inline constexpr bool tuple_invocable_v =
            tuple_invocable<F, std::remove_cvref_t<Tuple>>::value;

    }  // namespace detail

    template <class... Rs>
    class ZipRange final {
        using RangeTuple    = std::tuple<Rs*...>;
        using MutableIteratorTuple = std::tuple<decltype(std::begin(std::declval<Rs&>()))...>;
        using MutableSentinelTuple = std::tuple<decltype(std::end(std::declval<Rs&>()))...>;
        using ConstIteratorTuple = std::tuple<decltype(std::begin(std::declval<const Rs&>()))...>;
        using ConstSentinelTuple = std::tuple<decltype(std::end(std::declval<const Rs&>()))...>;

        template <bool Const>
        using IteratorTupleFor = std::conditional_t<
              Const
            , ConstIteratorTuple
            , MutableIteratorTuple
        >;

        template <bool Const>
        using SentinelTupleFor = std::conditional_t<
              Const
            , ConstSentinelTuple
            , MutableSentinelTuple
        >;

    public:
        explicit ZipRange(Rs&... ranges)
            : ranges_{ &ranges... } {
        }

        template <bool Const>
        class basic_iterator final {
            using IteratorTuple = IteratorTupleFor<Const>;
            using SentinelTuple = SentinelTupleFor<Const>;

        public:
            using difference_type = std::ptrdiff_t;

            basic_iterator(
                  IteratorTuple current
                , SentinelTuple end
                , bool          done
            )
                : current_(std::move(current))
                , end_(std::move(end))
                , done_(done) {
            }

            basic_iterator& operator++() {
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

            MATHFP_NODISCARD bool operator==(const basic_iterator& other) const {
                if (done_ && other.done_) {
                    return true;
                }
                return current_ == other.current_;
            }

            MATHFP_NODISCARD bool operator!=(const basic_iterator& other) const {
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

        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;

        MATHFP_NODISCARD iterator begin() {
            auto end        = make_end_tuple();
            auto begin      = make_begin_tuple();
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

        MATHFP_NODISCARD const_iterator begin() const {
            auto end        = make_cend_tuple();
            auto begin      = make_cbegin_tuple();
            const auto done = detail::any_iterator_at_end(begin, end);
            return const_iterator{
                  std::move(begin)
                , end
                , done
            };
        }

        MATHFP_NODISCARD const_iterator end() const {
            auto end = make_cend_tuple();
            return const_iterator{ end, end, true };
        }

    private:
        MATHFP_NODISCARD MutableIteratorTuple make_begin_tuple() const {
            return std::apply(
                [](auto*... ranges) {
                    return MutableIteratorTuple{ std::begin(*ranges)... };
                }
                , ranges_
            );
        }

        MATHFP_NODISCARD MutableSentinelTuple make_end_tuple() const {
            return std::apply(
                [](auto*... ranges) {
                    return MutableSentinelTuple{ std::end(*ranges)... };
                }
                , ranges_
            );
        }

        MATHFP_NODISCARD ConstIteratorTuple make_cbegin_tuple() const {
            return std::apply(
                [](auto*... ranges) {
                    return ConstIteratorTuple{ std::begin(std::as_const(*ranges))... };
                }
                , ranges_
            );
        }

        MATHFP_NODISCARD ConstSentinelTuple make_cend_tuple() const {
            return std::apply(
                [](auto*... ranges) {
                    return ConstSentinelTuple{ std::end(std::as_const(*ranges))... };
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
        using Function    = std::decay_t<F>;
        using ZippedRange = ZipRange<Rs...>;
        using MutableReference = decltype(*std::declval<ZippedRange&>().begin());
        using ConstReference = decltype(*std::declval<const ZippedRange&>().begin());

    public:
        ZipWithRange(F&& function, Rs&... ranges)
            : zipped_(ranges...)
            , fn_(std::forward<F>(function)) {
        }

        template <bool Const>
        class basic_iterator final {
            using BaseIterator = std::conditional_t<
                  Const
                , typename ZippedRange::const_iterator
                , typename ZippedRange::iterator
            >;
            using FunctionPointer = std::conditional_t<Const, const Function*, Function*>;

        public:
            basic_iterator(
                  BaseIterator current
                , FunctionPointer fn
            )
                : current_(std::move(current))
                , fn_(fn) {
            }

            basic_iterator& operator++() {
                ++current_;
                return *this;
            }

            void operator++(int) {
                ++(*this);
            }

            MATHFP_NODISCARD bool operator==(const basic_iterator& other) const {
                return current_ == other.current_;
            }

            MATHFP_NODISCARD bool operator!=(const basic_iterator& other) const {
                return !(*this == other);
            }

            MATHFP_NODISCARD decltype(auto) operator*() const {
                auto tuple = *current_;
                return std::apply(
                    [this](auto&&... xs) -> decltype(auto) {
                        return std::invoke(
                              *fn_
                            , std::forward<decltype(xs)>(xs)...
                        );
                    }
                    , tuple
                );
            }

        private:
            BaseIterator current_;
            FunctionPointer fn_{ nullptr };
        };

        using iterator = basic_iterator<false>;
        using const_iterator = basic_iterator<true>;

        MATHFP_NODISCARD iterator begin() {
            return iterator{ zipped_.begin(), &fn_ };
        }

        MATHFP_NODISCARD iterator end() {
            return iterator{ zipped_.end(), &fn_ };
        }

        MATHFP_NODISCARD const_iterator begin() const
            requires detail::tuple_invocable_v<const Function&, ConstReference>
        {
            return const_iterator{ zipped_.begin(), &fn_ };
        }

        MATHFP_NODISCARD const_iterator end() const
            requires detail::tuple_invocable_v<const Function&, ConstReference>
        {
            return const_iterator{ zipped_.end(), &fn_ };
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
