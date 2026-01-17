#pragma once

#ifndef MATHFP_HAS_BOOST_GRAPH
#  error "mathfp/graph requires Boost.Graph. Enable the graph feature (MATHFP_HAS_BOOST_GRAPH)."
#endif

#include <cstddef>
#include <type_traits>

#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>

#include <mathfp/types/index.hpp>

namespace mathfp::graph {

    // --- Strong IDs --------------------------------------------------------------
    // Единые доменные ID по всей библиотеке (не путать с индексами матриц и т.п.).

    struct VertexIdTag {};
    using VertexId = ::mathfp::Index<VertexIdTag>;

    // EdgeId намеренно НЕ даём как стабильный индекс.
    // В BGL edge_descriptor не обязан быть плотным индексом.
    template <class G>
    using Vertex = typename boost::graph_traits<G>::vertex_descriptor;

    template <class G>
    using Edge = typename boost::graph_traits<G>::edge_descriptor;

    // --- Canonical graph type ----------------------------------------------------
    // Мы стандартизируем graph как adjacency_list с vecS/vecS:
    // - vertex_descriptor становится индексом (size_t), что удобно
    // - есть vertex_index map
    // - быстро и предсказуемо
    //
    // Замечание: удаление вершин в vecS может инвалидировать индексы.

    template <class Weight, class DirectedS>
    using GraphT =
        boost::adjacency_list<
            boost::vecS                                      // out-edge container
            , boost::vecS                                    // vertex container (gives stable indices if you don't erase)
            , DirectedS                                      // directedS / undirectedS / bidirectionalS
            , boost::no_property                             // vertex properties
            , boost::property<boost::edge_weight_t, Weight>  // edge weight
        >;

    template <class Weight = double>
    using DiGraph = GraphT<Weight, boost::directedS>;

    template <class Weight = double>
    using UndiGraph = GraphT<Weight, boost::undirectedS>;

    // Weight type extraction for any graph that has edge_weight_t.
    template <class G>
    using WeightType = typename boost::property_traits<
        typename boost::property_map<G, boost::edge_weight_t>::const_type
    >::value_type;

}  // namespace mathfp::graph
