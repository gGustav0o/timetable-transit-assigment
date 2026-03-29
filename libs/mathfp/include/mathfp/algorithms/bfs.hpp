#pragma once

#if !defined(MATHFP_HAS_GRAPH) && !defined(MATHFP_HAS_INTEROP) && !defined(MATHFP_HAS_ALL)
#  error "mathfp/graph requires Boost.Graph. Enable the graph feature."
#endif

#include <cstddef>
#include <limits>
#include <queue>
#include <source_location>
#include <utility>
#include <vector>

#include <mathfp/compiler_attributes.hpp>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_iterator.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/graph/properties.hpp>
#include <mathfp/graph/types.hpp>

namespace mathfp::graph {

    struct BfsResult final {
        std::vector<VertexId>    order{};
        std::vector<VertexId>    parent{};
        std::vector<std::size_t> distance{};

        MATHFP_NODISCARD std::size_t size() const noexcept { return order.size(); }
    };

    namespace detail {

        inline constexpr std::size_t kInfDist = std::numeric_limits<std::size_t>::max();

    }  // namespace detail

    template <class G>
    MATHFP_NODISCARD inline ::mathfp::Expected<BfsResult> bfs(
          const G&             g
        , VertexId             start
        , std::source_location where = std::source_location::current()
    ) {
        const auto n = vertex_count(g);

        if (n == 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("graph must be non-empty", where)
                .ctx("num_vertices", n)
                .ctx("num_edges"   , edge_count(g)));
        }

        if (!::mathfp::is_valid(start)) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("start vertex id is invalid (sentinel)", where));
        }

        const auto s = ::mathfp::to_usize(start);
        if (s >= n) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("start vertex id out of range", where)
                .ctx("start"       , s)
                .ctx("num_vertices", n));
        }

        BfsResult res;
        res.order   .reserve(n);
        res.parent  .assign(n, ::mathfp::invalid_index<VertexIdTag>());
        res.distance.assign(n, detail::kInfDist);

        std::vector<unsigned char> visited(n, 0);

        std::queue<Vertex<G>> q;
        const auto sv = static_cast<Vertex<G>>(s);

        visited[s]      = 1;
        res.distance[s] = 0;
        res.parent[s]   = ::mathfp::invalid_index<VertexIdTag>();
        res.order.push_back(start);
        q.push(sv);

        while (!q.empty()) {
            const auto u = q.front();
            q.pop();

            auto [it, it_end] = boost::adjacent_vertices(u, g);
            for (; it != it_end; ++it) {
                const auto v   = *it;
                const auto vid = vertex_id(g, v);
                const auto vi  = ::mathfp::to_usize(vid);

                if (!visited[vi]) {
                    visited[vi]      = 1;
                    res.parent[vi]   = vertex_id(g, u);
                    res.distance[vi] = res.distance[::mathfp::to_usize(res.parent[vi])] + 1;
                    res.order.push_back(vid);
                    q.push(v);
                }
            }
        }

        return res;
    }

}  // namespace mathfp::graph

