// include/mathfp/graph/build.hpp
#pragma once

#if !defined(MATHFP_HAS_GRAPH) && !defined(MATHFP_HAS_INTEROP) && !defined(MATHFP_HAS_ALL)
#  error "mathfp/graph requires Boost.Graph. Enable the graph feature."
#endif

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <mathfp/compiler_attributes.hpp>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/graph/properties.hpp>
#include <mathfp/graph/types.hpp>

namespace mathfp::graph {

    // -------------------- Input edge record --------------------------------------

    template <class Weight>
    struct WeightedEdge final {
        VertexId u;
        VertexId v;
        Weight   w;
    };

    // -------------------- Build policy -------------------------------------------

    enum class BuildPolicy : unsigned char {
        Strict,  // ids must be < num_vertices_hint
        Grow,    // auto-expand to max(id)+1
    };

    namespace detail {

        inline constexpr std::string_view kPolicy = "policy";

        MATHFP_NODISCARD inline std::string_view to_string(BuildPolicy p) noexcept {
            switch (p) {
            case BuildPolicy::Strict: return "Strict";
            case BuildPolicy::Grow:   return "Grow";
            }
            return "Strict";
        }

        template <class G>
        MATHFP_NODISCARD inline ::mathfp::Expected<::mathfp::Unit> ensure_vertex_count(
              G& g
            , std::size_t n
            , std::source_location = std::source_location::current()
        ) {
            const auto cur = vertex_count(g);
            if (cur >= n) return ::mathfp::kUnit;

            const auto add = n - cur;
            for (std::size_t i = 0; i < add; ++i) {
                boost::add_vertex(g);
            }
            return ::mathfp::kUnit;
        }

        template <class Weight>
        MATHFP_NODISCARD inline std::size_t max_vertex_id_in_edges(
            const std::vector<WeightedEdge<Weight>>& edges
        ) {
            std::size_t m = 0;
            for (const auto& e : edges) {
                m = std::max(m, std::max(::mathfp::to_usize(e.u), ::mathfp::to_usize(e.v)));
            }
            return m;
        }

        template <class Weight>
        MATHFP_NODISCARD inline ::mathfp::Expected<::mathfp::Unit> validate_edge_ids(
              const WeightedEdge<Weight>& e
            , BuildPolicy                 policy
            , std::size_t                 num_vertices
            , std::source_location        where
        ) {
            if (policy == BuildPolicy::Strict) {
                const auto u = ::mathfp::to_usize(e.u);
                const auto v = ::mathfp::to_usize(e.v);
                if (u >= num_vertices || v >= num_vertices) {
                    return ::mathfp::unexpected(
                        ::mathfp::invalid_arg("vertex id out of range", where)
                        .ctx("u"           , u)
                        .ctx("v"           , v)
                        .ctx("num_vertices", num_vertices)
                        .ctx(kPolicy       , to_string(policy)));
                }
            }
            return ::mathfp::kUnit;
        }

    }  // namespace detail

    template <class G, class Weight, std::ranges::input_range R>
    MATHFP_NODISCARD inline ::mathfp::Expected<G> build_from_edges(
          std::size_t num_vertices_hint
        , R&& edges
        , BuildPolicy policy = BuildPolicy::Strict
        , std::source_location where = std::source_location::current()
    ) requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, WeightedEdge<Weight>> {
        if (policy == BuildPolicy::Strict && num_vertices_hint == 0) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("num_vertices_hint must be positive in Strict mode", where)
                .ctx("num_vertices_hint", num_vertices_hint)
                .ctx(detail::kPolicy    , detail::to_string(policy)));
        }

        G g;
        if (num_vertices_hint > 0) {
            MATHFP_TRY(detail::ensure_vertex_count(g, num_vertices_hint, where));
        }

        if constexpr (!std::ranges::forward_range<R>) {
            std::vector<WeightedEdge<Weight>> tmp;
            for (const auto& e : edges) tmp.push_back(e);

            if (policy == BuildPolicy::Grow) {
                const auto max_id = detail::max_vertex_id_in_edges(tmp);
                MATHFP_TRY(detail::ensure_vertex_count(g, max_id + 1, where));
            }

            for (const auto& e : tmp) {
                MATHFP_TRY(detail::validate_edge_ids(e, policy, vertex_count(g), where));

                if (policy == BuildPolicy::Grow) {
                    const auto need = std::max(::mathfp::to_usize(e.u), ::mathfp::to_usize(e.v)) + 1;
                    MATHFP_TRY(detail::ensure_vertex_count(g, need, where));
                }

                const auto u = static_cast<Vertex<G>>(::mathfp::to_usize(e.u));
                const auto v = static_cast<Vertex<G>>(::mathfp::to_usize(e.v));

                const auto [ed, ok] = boost::add_edge(u, v, g);
                if (!ok) {
                    return ::mathfp::unexpected(
                        ::mathfp::internal_error("boost::add_edge failed", where)
                        .ctx("u"            , ::mathfp::to_usize(e.u))
                        .ctx("v"            , ::mathfp::to_usize(e.v))
                        .ctx(detail::kPolicy, detail::to_string(policy)));
                }

                weight(g, ed) = static_cast<WeightType<G>>(e.w);
            }

            return g;
        }
        else {
            if (policy == BuildPolicy::Grow) {
                std::size_t max_id = 0;
                for (const auto& e : edges) {
                    max_id = std::max(max_id, std::max(::mathfp::to_usize(e.u), ::mathfp::to_usize(e.v)));
                }
                MATHFP_TRY(detail::ensure_vertex_count(g, max_id + 1, where));
            }

            for (const auto& e : edges) {
                MATHFP_TRY(detail::validate_edge_ids(e, policy, vertex_count(g), where));

                if (policy == BuildPolicy::Grow) {
                    const auto need = std::max(::mathfp::to_usize(e.u), ::mathfp::to_usize(e.v)) + 1;
                    MATHFP_TRY(detail::ensure_vertex_count(g, need, where));
                }

                const auto u = static_cast<Vertex<G>>(::mathfp::to_usize(e.u));
                const auto v = static_cast<Vertex<G>>(::mathfp::to_usize(e.v));

                const auto [ed, ok] = boost::add_edge(u, v, g);
                if (!ok) {
                    return ::mathfp::unexpected(
                        ::mathfp::internal_error("boost::add_edge failed", where)
                        .ctx("u"            , ::mathfp::to_usize(e.u))
                        .ctx("v"            , ::mathfp::to_usize(e.v))
                        .ctx(detail::kPolicy, detail::to_string(policy)));
                }

                weight(g, ed) = static_cast<WeightType<G>>(e.w);
            }

            return g;
        }
    }

    template <class G, class Weight, std::ranges::input_range R>
    MATHFP_NODISCARD inline ::mathfp::Expected<G> build_from_edges_infer_vertices(
          R&& edges
        , std::source_location where = std::source_location::current()
    ) requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<R>>, WeightedEdge<Weight>> {
        return build_from_edges<G, Weight>(0, std::forward<R>(edges), BuildPolicy::Grow, where);
    }

}  // namespace mathfp::graph

