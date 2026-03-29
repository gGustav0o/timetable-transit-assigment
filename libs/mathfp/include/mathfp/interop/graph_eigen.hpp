// include/mathfp/interop/graph_eigen.hpp
#pragma once

#if !defined(MATHFP_HAS_GRAPH) && !defined(MATHFP_HAS_INTEROP) && !defined(MATHFP_HAS_ALL)
#  error "mathfp/interop/graph_eigen requires Boost.Graph (graph feature)."
#endif

#if !defined(MATHFP_HAS_LINALG) && !defined(MATHFP_HAS_INTEROP) && !defined(MATHFP_HAS_ALL)
#  error "mathfp/interop/graph_eigen requires Eigen (linalg feature)."
#endif

#include <cmath>
#include <cstddef>
#include <source_location>
#include <type_traits>
#include <utility>
#include <vector>

#include <mathfp/compiler_attributes.hpp>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/graph/properties.hpp>
#include <mathfp/graph/types.hpp>
#include <mathfp/linalg/sparse_build.hpp>
#include <mathfp/linalg/types.hpp>

namespace mathfp::interop {

    namespace detail {

        inline constexpr const char* kMethod = "method";

        template <class W>
        MATHFP_NODISCARD inline bool is_finite(const W& w) {
            if constexpr (std::is_floating_point_v<W>) {
                return std::isfinite(w);
            }
            else {
                return true;
            }
        }

        template <class W>
        MATHFP_NODISCARD constexpr bool is_negative(const W& w) {
            if constexpr (std::is_floating_point_v<W>) {
                // NaN checked separately
                return w < static_cast<W>(0);
            }
            else if constexpr (std::is_signed_v<W>) {
                return w < static_cast<W>(0);
            }
            else {
                return false;
            }
        }

        template <class G>
        MATHFP_NODISCARD inline Eigen::Index n_vertices_eigen(const G& g) {
            return static_cast<Eigen::Index>(::mathfp::graph::vertex_count(g));
        }

        template <class G>
        MATHFP_NODISCARD inline ::mathfp::Expected<::mathfp::Unit> ensure_graph_nonempty_or_allow_empty(
              const G&
            , bool
            , std::source_location
        ) {
            return ::mathfp::kUnit;
        }

        template <class G>
        constexpr bool is_undirected_v =
            std::is_same_v<typename boost::graph_traits<G>::directed_category, boost::undirected_tag>;

    }  // namespace detail

    // -------------------- Adjacency (sparse) -------------------------------------
    template <class Scalar = double, class G>
    MATHFP_NODISCARD inline ::mathfp::Expected<::mathfp::linalg::SpMat<Scalar>> adjacency_sparse(
          const G&             g
        , bool                 symmetrize_undirected = true
        , std::source_location where = std::source_location::current()
    ) {
        MATHFP_TRY(detail::ensure_graph_nonempty_or_allow_empty(g, true, where));

        const auto n = detail::n_vertices_eigen(g);
        using Trip   = ::mathfp::linalg::Triplet<Scalar>;

        std::vector<Trip> t;
        t.reserve(static_cast<std::size_t>(::mathfp::graph::edge_count(g)) *
            (detail::is_undirected_v<G> && symmetrize_undirected ? 2u : 1u));

        auto [eit, eend] = boost::edges(g);
        for (; eit != eend; ++eit) {
            const auto e = *eit;

            const auto u_id = ::mathfp::graph::source_id(g, e);
            const auto v_id = ::mathfp::graph::target_id(g, e);
            const auto ui   = static_cast<Eigen::Index>(::mathfp::to_usize(u_id));
            const auto vi   = static_cast<Eigen::Index>(::mathfp::to_usize(v_id));

            const auto w_raw = ::mathfp::graph::weight(g, e);
            const Scalar w   = static_cast<Scalar>(w_raw);

            if (!detail::is_finite(w)) {
                return ::mathfp::unexpected(
                    ::mathfp::domain_error("edge weight is NaN/Inf", where)
                    .ctx("u"            , ::mathfp::to_usize(u_id))
                    .ctx("v"            , ::mathfp::to_usize(v_id))
                    .ctx("w"            , w)
                    .ctx(detail::kMethod, "adjacency_sparse"));
            }

            t.emplace_back(ui, vi, w);

            if constexpr (detail::is_undirected_v<G>) {
                if (symmetrize_undirected && ui != vi) {
                    t.emplace_back(vi, ui, w);
                }
            }
        }

        return ::mathfp::linalg::build_sparse<Scalar>(n, n, t, ::mathfp::linalg::DuplicatePolicy::Sum, where);
    }

    // -------------------- Laplacian (combinatorial, undirected) ------------------
    template <class Scalar = double, class G>
    MATHFP_NODISCARD inline ::mathfp::Expected<::mathfp::linalg::SpMat<Scalar>> laplacian_sparse(
          const G& g
        , std::source_location where = std::source_location::current()
    ) {
        if constexpr (!detail::is_undirected_v<G>) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("laplacian_sparse expects an undirected graph", where)
                .ctx(detail::kMethod, "laplacian_sparse"));
        }

        const auto n = detail::n_vertices_eigen(g);
        using Trip   = ::mathfp::linalg::Triplet<Scalar>;

        // degree accumulators
        std::vector<Scalar> deg(static_cast<std::size_t>(n), Scalar{ 0 });

        // off-diagonal entries
        std::vector<Trip> t;
        t.reserve(static_cast<std::size_t>(::mathfp::graph::edge_count(g)) * 2u + static_cast<std::size_t>(n));

        auto [eit, eend] = boost::edges(g);
        for (; eit != eend; ++eit) {
            const auto e = *eit;

            const auto u_id = ::mathfp::graph::source_id(g, e);
            const auto v_id = ::mathfp::graph::target_id(g, e);
            const auto ui   = static_cast<Eigen::Index>(::mathfp::to_usize(u_id));
            const auto vi   = static_cast<Eigen::Index>(::mathfp::to_usize(v_id));

            const auto w_raw = ::mathfp::graph::weight(g, e);
            const Scalar w   = static_cast<Scalar>(w_raw);

            if (!detail::is_finite(w)) {
                return ::mathfp::unexpected(
                    ::mathfp::domain_error("edge weight is NaN/Inf", where)
                    .ctx("u"            , ::mathfp::to_usize(u_id))
                    .ctx("v"            , ::mathfp::to_usize(v_id))
                    .ctx("w"            , w)
                    .ctx(detail::kMethod, "laplacian_sparse"));
            }
            if (detail::is_negative(w)) {
                return ::mathfp::unexpected(
                    ::mathfp::invalid_arg("negative weight is not allowed for Laplacian", where)
                    .ctx("u"            , ::mathfp::to_usize(u_id))
                    .ctx("v"            , ::mathfp::to_usize(v_id))
                    .ctx("w"            , w)
                    .ctx(detail::kMethod, "laplacian_sparse"));
            }
            if (ui == vi) {
                return ::mathfp::unexpected(
                    ::mathfp::invalid_arg("self-loops are not allowed for Laplacian", where)
                    .ctx("v"            , ::mathfp::to_usize(u_id))
                    .ctx("w"            , w)
                    .ctx(detail::kMethod, "laplacian_sparse"));
            }

            // L off-diagonal: -w for both symmetric entries
            t.emplace_back(ui, vi, -w);
            t.emplace_back(vi, ui, -w);

            // degree
            deg[static_cast<std::size_t>(ui)] += w;
            deg[static_cast<std::size_t>(vi)] += w;
        }

        // diagonal: degree
        for (Eigen::Index i = 0; i < n; ++i) {
            const auto d = deg[static_cast<std::size_t>(i)];
            if (d != Scalar{ 0 }) {
                t.emplace_back(i, i, d);
            }
        }

        return ::mathfp::linalg::build_sparse<Scalar>(n, n, t, ::mathfp::linalg::DuplicatePolicy::Sum, where);
    }

}  // namespace mathfp::interop
