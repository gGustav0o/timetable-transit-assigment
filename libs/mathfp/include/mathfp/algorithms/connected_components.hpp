#pragma once

#ifndef MATHFP_HAS_BOOST_GRAPH
#  error "mathfp/graph requires Boost.Graph. Enable the graph feature (MATHFP_HAS_BOOST_GRAPH)."
#endif

#include <cstddef>
#include <source_location>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/graph/connected_components.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/property_map/property_map.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/graph/properties.hpp>
#include <mathfp/graph/types.hpp>

namespace mathfp::graph {

    struct ConnectedComponentsResult final {
        std::vector<std::size_t> component{};
        std::size_t count = 0;
    };

    namespace detail {
        inline constexpr const char* kMethod = "method";

        template <class G>
        constexpr bool is_directed_v =
            std::is_same_v<typename boost::graph_traits<G>::directed_category, boost::directed_tag> ||
            std::is_same_v<typename boost::graph_traits<G>::directed_category, boost::bidirectional_tag>;
    }  // namespace detail

    template <class G>
    [[nodiscard]] inline ::mathfp::Expected<ConnectedComponentsResult> connected_components(
        const G& g
        ,std::source_location where = std::source_location::current()
    ) {
        if constexpr (detail::is_directed_v<G>) {
            return ::mathfp::unexpected(
                ::mathfp::invalid_arg("connected_components expects an undirected graph (use SCC for directed)", where)
                .ctx(detail::kMethod, "connected_components"));
        }

        const auto n = vertex_count(g);
        ConnectedComponentsResult res;
        res.component.assign(n, 0);

        if (n == 0) {
            res.count = 0;
            return res;
        }

        auto index_map = boost::get(boost::vertex_index, g);
        auto comp_map = boost::make_iterator_property_map(res.component.begin(), index_map);

        res.count = static_cast<std::size_t>(boost::connected_components(g, comp_map));

        return res;
    }

}  // namespace mathfp::graph
