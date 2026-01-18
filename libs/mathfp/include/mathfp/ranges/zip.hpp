#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include <mathfp/compiler_attributes.hpp>
#include <mathfp/ranges/boost.hpp>

namespace mathfp::ranges {
    namespace detail {

        template <class BoostTuple, std::size_t... Is>
        MATHFP_NODISCARD constexpr auto boost_tuple_to_std_tuple_impl(BoostTuple&& t, std::index_sequence<Is...>) {
            return std::tuple<decltype(boost::get<Is>(std::forward<BoostTuple>(t)))...>{
                boost::get<Is>(std::forward<BoostTuple>(t))...};
        }

        template <std::size_t N, class BoostTuple>
        MATHFP_NODISCARD constexpr auto boost_tuple_to_std_tuple(BoostTuple&& t) {
            return boost_tuple_to_std_tuple_impl(std::forward<BoostTuple>(t), std::make_index_sequence<N>{});
        }

    }  // namespace detail

    template <class CombinedRange, std::size_t N>
    class ZipRange final {
    public:
        explicit ZipRange(CombinedRange cr) : cr_(std::move(cr)) {}

        class iterator final {
            using base_iterator = decltype(std::begin(std::declval<CombinedRange&>()));
        public:
            using difference_type = std::ptrdiff_t;

            explicit iterator(base_iterator it) : it_(std::move(it)) {}

            iterator& operator++() {
                ++it_;
                return *this;
            }

            void operator++(int) { ++(*this); }

            MATHFP_NODISCARD friend bool operator==(const iterator& a, const iterator& b) {
                return a.it_ == b.it_;
            }
            MATHFP_NODISCARD friend bool operator!=(const iterator& a, const iterator& b) {
                return !(a == b);
            }

            MATHFP_NODISCARD auto operator*() const {
                auto&& bt = *it_;
                return detail::boost_tuple_to_std_tuple<N>(bt);
            }

        private:
            base_iterator it_;
        };

        MATHFP_NODISCARD iterator begin() { return iterator{ std::begin(cr_) }; }
        MATHFP_NODISCARD iterator end() { return iterator{ std::end(cr_) }; }

        MATHFP_NODISCARD iterator begin() const { return iterator{ std::begin(cr_) }; }
        MATHFP_NODISCARD iterator end() const { return iterator{ std::end(cr_) }; }

    private:
        CombinedRange cr_;
    };

    template <class... Rs>
    MATHFP_NODISCARD inline auto zip(Rs&... rs) {
        auto combined = boost::combine(rs...);
        using CombinedRange = decltype(combined);
        return ZipRange<CombinedRange, sizeof...(Rs)>(std::move(combined));
    }

    template <class F, class... Rs>
    MATHFP_NODISCARD inline auto zip_with(F&& f, Rs&... rs) {
        struct Range final {
            using Z = decltype(zip(rs...));
            Z z;
            std::decay_t<F> fn;

            class iterator final {
                using base_it = decltype(std::declval<Z&>().begin());
            public:
                explicit iterator(base_it it, std::decay_t<F>* fn) : it_(it), fn_(fn) {}
                iterator& operator++() { ++it_; return *this; }
                void operator++(int) { ++(*this); }

                MATHFP_NODISCARD friend bool operator==(const iterator& a, const iterator& b) { return a.it_ == b.it_; }
                MATHFP_NODISCARD friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }

                MATHFP_NODISCARD decltype(auto) operator*() const {
                    auto tup = *it_;
                    return std::apply(*fn_, tup);
                }
            private:
                base_it it_;
                std::decay_t<F>* fn_;
            };

            MATHFP_NODISCARD iterator begin() { return iterator{ z.begin(), &fn }; }
            MATHFP_NODISCARD iterator end() { return iterator{ z.end(), &fn }; }
        };

        return Range{ zip(rs...), std::forward<F>(f) };
    }

}  // namespace mathfp::ranges
