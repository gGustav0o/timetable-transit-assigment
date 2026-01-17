#pragma once

#ifndef MATHFP_HAS_BOOST_GRAPH
#  error "mathfp/graph requires Boost.Graph. Enable the graph feature (MATHFP_HAS_BOOST_GRAPH)."
#endif

#include <cstddef>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/graph_utility.hpp>
#include <boost/property_map/property_map.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/core/utility.hpp>
#include <mathfp/types/index.hpp>
#include <mathfp/graph/types.hpp>

namespace mathfp::graph {

    // -------------------- vertex id <-> descriptor -------------------------------

    template <class G>
    [[nodiscard]] inline VertexId vertex_id(const G& g, Vertex<G> v) {
        // vertex_index map гарантирован для adjacency_list(vecS,vecS,...) и почти всех sane графов.
        const auto idx = static_cast<std::size_t>(boost::get(boost::vertex_index, g, v));
        return VertexId(idx);
    }

    template <class G>
    [[nodiscard]] inline ::mathfp::Expected<Vertex<G>> vertex_from_id(
        const G& g,
        VertexId id,
        std::source_location where = std::source_location::current()) {
        if (!::mathfp::is_valid(id)) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("vertex id is invalid (sentinel)", where));
        }

        const auto n = static_cast<std::size_t>(boost::num_vertices(g));
        const auto i = ::mathfp::to_usize(id);

        if (i >= n) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("vertex id out of range", where)
                .ctx("v", i)
                .ctx("num_vertices", n));
        }

        return static_cast<Vertex<G>>(i);
    }

    // -------------------- edges, endpoints --------------------------------------

    template <class G>
    [[nodiscard]] inline Vertex<G> source(const G& g, Edge<G> e) {
        return boost::source(e, g);
    }

    template <class G>
    [[nodiscard]] inline Vertex<G> target(const G& g, Edge<G> e) {
        return boost::target(e, g);
    }

    template <class G>
    [[nodiscard]] inline VertexId source_id(const G& g, Edge<G> e) {
        return vertex_id(g, source(g, e));
    }

    template <class G>
    [[nodiscard]] inline VertexId target_id(const G& g, Edge<G> e) {
        return vertex_id(g, target(g, e));
    }

    // -------------------- weight access (hide property maps) ---------------------

    template <class G>
    [[nodiscard]] inline decltype(auto) weight(G& g, Edge<G> e) {
        return boost::get(boost::edge_weight, g, e);
    }

    template <class G>
    [[nodiscard]] inline decltype(auto) weight(const G& g, Edge<G> e) {
        return boost::get(boost::edge_weight, g, e);
    }

    // -------------------- size helpers ------------------------------------------

    template <class G>
    [[nodiscard]] inline std::size_t vertex_count(const G& g) {
        return static_cast<std::size_t>(boost::num_vertices(g));
    }

    template <class G>
    [[nodiscard]] inline std::size_t edge_count(const G& g) {
        return static_cast<std::size_t>(boost::num_edges(g));
    }

    // -------------------- common checks ------------------------------------------

    template <class G>
    [[nodiscard]] inline ::mathfp::Expected<::mathfp::Unit> ensure_nonempty_graph(
        const G& g
        , std::source_location where = std::source_location::current()
    ) {
        const auto n = vertex_count(g);
        if (n > 0) return ::mathfp::kUnit;

        return ::mathfp::unexpected(
            ::mathfp::invalid_arg("graph must be non-empty", where)
            .ctx("num_vertices", n)
            .ctx("num_edges", edge_count(g)));
    }

}  // namespace mathfp::graph
