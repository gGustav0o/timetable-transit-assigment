#pragma once

#if !defined(MATHFP_HAS_GRAPH) && !defined(MATHFP_HAS_INTEROP) && !defined(MATHFP_HAS_ALL)
#  error "mathfp/interop/boost_property_maps requires Boost.Graph."
#endif

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include <mathfp/compiler_attributes.hpp>

#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>
#include <boost/property_map/property_map.hpp>

#include <mathfp/graph/properties.hpp>
#include <mathfp/graph/types.hpp>

namespace mathfp::interop {

    namespace detail {

        template <class G>
        using Vertex = typename boost::graph_traits<G>::vertex_descriptor;

        template <class G>
        using VertexIndexMap = typename boost::property_map<G, boost::vertex_index_t>::const_type;

        template <class G>
        MATHFP_NODISCARD inline VertexIndexMap<G> vertex_index_map(const G& g) {
            return boost::get(boost::vertex_index, g);
        }

        template <class G, class T>
        using IteratorPMap =
            boost::iterator_property_map<typename std::vector<T>::iterator, VertexIndexMap<G>>;

        template <class G, class T>
        using ConstIteratorPMap =
            boost::iterator_property_map<typename std::vector<T>::const_iterator, VertexIndexMap<G>>;

    }  // namespace detail

    template <class G, class T>
    class VertexPropertyVec final {
    public:
        using value_type = T;
        using graph_type = G;

        VertexPropertyVec() = default;

        explicit VertexPropertyVec(const G& g, T init = T{})
            : data_(::mathfp::graph::vertex_count(g), std::move(init)) {
        }

        MATHFP_NODISCARD std::size_t size() const noexcept { return data_.size(); }

        MATHFP_NODISCARD std::vector<T>& data() noexcept { return data_; }
        MATHFP_NODISCARD const std::vector<T>& data() const noexcept { return data_; }

        MATHFP_NODISCARD auto pmap(const G& g) {
            return boost::make_iterator_property_map(data_.begin(), detail::vertex_index_map(g));
        }

        MATHFP_NODISCARD auto pmap(const G& g) const {
            return boost::make_iterator_property_map(data_.begin(), detail::vertex_index_map(g));
        }

        MATHFP_NODISCARD T& at(::mathfp::graph::VertexId v) { return data_.at(::mathfp::to_usize(v)); }
        MATHFP_NODISCARD const T& at(::mathfp::graph::VertexId v) const { return data_.at(::mathfp::to_usize(v)); }

    private:
        std::vector<T> data_{};
    };

    template <class G, class T>
    MATHFP_NODISCARD inline auto make_vertex_pmap(const G& g, std::vector<T>& vec) {
        return boost::make_iterator_property_map(vec.begin(), detail::vertex_index_map(g));
    }

    template <class G>
    MATHFP_NODISCARD inline std::vector<::mathfp::graph::VertexId> to_vertex_ids(
        const G& g,
        const std::vector<typename boost::graph_traits<G>::vertex_descriptor>& vs) {
        std::vector<::mathfp::graph::VertexId> out;
        out.reserve(vs.size());
        for (auto v : vs) out.push_back(::mathfp::graph::vertex_id(g, v));
        return out;
    }

}  // namespace mathfp::interop


