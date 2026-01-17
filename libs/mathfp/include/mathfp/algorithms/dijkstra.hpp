#pragma once

#ifndef MATHFP_HAS_BOOST_GRAPH
#  error "mathfp/graph requires Boost.Graph. Enable the graph feature (MATHFP_HAS_BOOST_GRAPH)."
#endif

#include <cmath>
#include <cstddef>
#include <limits>
#include <source_location>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/graph/dijkstra_shortest_paths.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>
#include <boost/property_map/property_map.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/graph/properties.hpp>
#include <mathfp/graph/types.hpp>
#include <mathfp/types/index.hpp>

namespace mathfp::graph {

    template <class Weight>
    struct DijkstraResultT final {
        std::vector<Weight> distance{};     // inf if unreachable
        std::vector<VertexId> parent{};     // invalid if unreachable or start
    };

    namespace detail {

        inline constexpr std::string_view kMethod = "method";

        template <class W>
        [[nodiscard]] constexpr W inf_value() {
            if constexpr (std::numeric_limits<W>::has_infinity) {
                return std::numeric_limits<W>::infinity();
            }
            else {
                return std::numeric_limits<W>::max();
            }
        }

        template <class W>
        [[nodiscard]] constexpr bool is_negative(const W& w) {
            if constexpr (std::is_floating_point_v<W>) {
                // NaN handled separately. Here it's a plain negative check.
                return w < static_cast<W>(0);
            }
            else if constexpr (std::is_signed_v<W>) {
                return w < static_cast<W>(0);
            }
            else {
                return false;
            }
        }

        template <class W>
        [[nodiscard]] inline bool is_finite(const W& w) {
            if constexpr (std::is_floating_point_v<W>) {
                return std::isfinite(w);
            }
            else {
                return true;
            }
        }

        template <class G>
        [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> validate_start(
            const G& g
            , VertexId start, std::source_location where
        ) {
            const auto n = vertex_count(g);
            if (n == 0) {
                return ::mathfp::unexpected(
                    ::mathfp::invalid_arg("graph must be non-empty", where)
                    .ctx("num_vertices", n)
                    .ctx("num_edges", edge_count(g))
                    .ctx(kMethod, "dijkstra"));
            }
            if (!::mathfp::is_valid(start)) {
                return ::mathfp::unexpected(
                    ::mathfp::invalid_arg("start vertex id is invalid (sentinel)", where)
                    .ctx(kMethod, "dijkstra"));
            }
            const auto s = ::mathfp::to_usize(start);
            if (s >= n) {
                return ::mathfp::unexpected(
                    ::mathfp::invalid_arg("start vertex id out of range", where)
                    .ctx("start", s)
                    .ctx("num_vertices", n)
                    .ctx(kMethod, "dijkstra"));
            }
            return ::mathfp::kUnit;
        }

        template <class G>
        [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> validate_weights_for_dijkstra(
            const G& g
            , std::source_location where
        ) {
            using W = WeightType<G>;

            auto [eit, eend] = boost::edges(g);
            for (; eit != eend; ++eit) {
                const auto e = *eit;
                const W w = static_cast<W>(weight(g, e));

                if (!is_finite(w)) {
                    return ::mathfp::unexpected(
                        ::mathfp::domain_error("edge weight is NaN/Inf", where)
                        .ctx("u", ::mathfp::to_usize(source_id(g, e)))
                        .ctx("v", ::mathfp::to_usize(target_id(g, e)))
                        .ctx("w", w)
                        .ctx(kMethod, "dijkstra"));
                }
                if (is_negative(w)) {
                    return ::mathfp::unexpected(
                        ::mathfp::invalid_arg("negative edge weight is not allowed for dijkstra", where)
                        .ctx("u", ::mathfp::to_usize(source_id(g, e)))
                        .ctx("v", ::mathfp::to_usize(target_id(g, e)))
                        .ctx("w", w)
                        .ctx(kMethod, "dijkstra"));
                }
            }
            return ::mathfp::kUnit;
        }

    }  // namespace detail

    template <class G>
    using DijkstraResult = DijkstraResultT<WeightType<G>>;

    // dijkstra: shortest paths from start.
    // - distance[v] = shortest path length (inf if unreachable)
    // - parent[v] = predecessor vertex id in shortest path tree (invalid if unreachable or start)
    template <class G>
    [[nodiscard]] inline ::mathfp::Expected<DijkstraResult<G>> dijkstra(
        const G& g
        , VertexId start
        , std::source_location where = std::source_location::current()
    ) {
        MATHFP_TRY(detail::validate_start(g, start, where));
        MATHFP_TRY(detail::validate_weights_for_dijkstra(g, where));

        using V = Vertex<G>;
        using W = WeightType<G>;

        const auto n = vertex_count(g);
        const auto s_idx = ::mathfp::to_usize(start);
        const V s = static_cast<V>(s_idx);

        std::vector<W> dist(n, detail::inf_value<W>());
        std::vector<V> pred(n, s);

        auto index_map = boost::get(boost::vertex_index, g);

        auto dist_map = boost::make_iterator_property_map(dist.begin(), index_map);
        auto pred_map = boost::make_iterator_property_map(pred.begin(), index_map);
        auto w_map = boost::get(boost::edge_weight, g);

        boost::dijkstra_shortest_paths(
            g, s,
            boost::distance_map(dist_map).predecessor_map(pred_map).weight_map(w_map));

        DijkstraResult<G> res;
        res.distance = std::move(dist);
        res.parent.assign(n, ::mathfp::invalid_index<VertexIdTag>());

        const auto inf = detail::inf_value<W>();
        for (std::size_t i = 0; i < n; ++i) {
            if (i == s_idx) {
                res.parent[i] = ::mathfp::invalid_index<VertexIdTag>();
                continue;
            }
            if (res.distance[i] == inf) {
                res.parent[i] = ::mathfp::invalid_index<VertexIdTag>();
                continue;
            }
            const V pv = pred[i];
            const auto pid = vertex_id(g, pv);
            // Если алгоритм оставил предка как "self", это либо start, либо странный случай.
            if (::mathfp::to_usize(pid) == i) {
                res.parent[i] = ::mathfp::invalid_index<VertexIdTag>();
            }
            else {
                res.parent[i] = pid;
            }
        }

        return res;
    }

}  // namespace mathfp::graph
