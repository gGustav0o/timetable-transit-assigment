#pragma once

#ifndef MATHFP_HAS_BOOST_GRAPH
#  error "mathfp/graph requires Boost.Graph. Enable the graph feature (MATHFP_HAS_BOOST_GRAPH)."
#endif

#include <algorithm>
#include <cstddef>
#include <source_location>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/is_directed_acyclic_graph.hpp>
#include <boost/graph/topological_sort.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/graph/properties.hpp>
#include <mathfp/graph/types.hpp>

namespace mathfp::graph {

    namespace detail {
        inline constexpr const char* kMethod = "method";

        template <class G>
        constexpr bool is_undirected_v =
            std::is_same_v<typename boost::graph_traits<G>::directed_category, boost::undirected_tag>;
    }  // namespace detail

    template <class G>
    [[nodiscard]] inline ::mathfp::Expected<std::vector<VertexId>> toposort(
        const G& g,
        std::source_location where = std::source_location::current()) {
        static_assert(!detail::is_undirected_v<G>,
            "toposort requires a directed graph (directedS or bidirectionalS).");

        const auto n = vertex_count(g);

        if (n == 0) {
            return std::vector<VertexId>{};
        }

        if (!boost::is_directed_acyclic_graph(g)) {
            return ::mathfp::unexpected(
                ::mathfp::domain_error("graph contains a cycle (not a DAG)", where)
                .ctx("num_vertices", n)
                .ctx("num_edges", edge_count(g))
                .ctx(detail::kMethod, "toposort"));
        }

        using V = Vertex<G>;
        std::vector<V> order_desc;
        order_desc.reserve(n);

        boost::topological_sort(g, std::back_inserter(order_desc));
        std::reverse(order_desc.begin(), order_desc.end());

        std::vector<VertexId> order;
        order.reserve(n);
        for (auto v : order_desc) {
            order.push_back(vertex_id(g, v));
        }

        return order;
    }

}  // namespace mathfp::graph
