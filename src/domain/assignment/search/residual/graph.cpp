#include "timetable/domain/assignment/search/residual/graph.hpp"

#include <cstddef>
#include <unordered_map>

#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
namespace {

    struct ResidualPhysicalEdgeKey final {
        EndpointKey from{};
        EndpointKey to{};
    };

    [[nodiscard]] bool operator==(
          const ResidualPhysicalEdgeKey& lhs
        , const ResidualPhysicalEdgeKey& rhs
    ) noexcept {
        return lhs.from == rhs.from && lhs.to == rhs.to;
    }

    struct ResidualPhysicalEdgeKeyHash final {
        std::size_t operator()(const ResidualPhysicalEdgeKey& key) const noexcept {
            std::size_t seed = 17u;
            seed = seed * 31u + std::hash<EndpointKey>{}(key.from);
            seed = seed * 31u + std::hash<EndpointKey>{}(key.to);
            return seed;
        }
    };

    using ResidualPhysicalEdgeTimes = std::unordered_map<
          ResidualPhysicalEdgeKey
        , Time
        , ResidualPhysicalEdgeKeyHash
    >;

    void retain_min_residual_edge_time(
          ResidualPhysicalEdgeTimes& edge_times
        , ResidualPhysicalEdgeKey    key
        , Time                       run_time
    ) {
        const auto [it, inserted] = edge_times.emplace(key, run_time);
        if (!inserted && run_time.value() < it->second.value()) {
            it->second = run_time;
        }
    }

    void append_residual_reverse_edges(
          std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>>& target
        , const ResidualPhysicalEdgeTimes&                                   edge_times
    ) {
        for (const auto& [key, run_time] : edge_times) {
            target[key.to].push_back(
                ResidualReverseEdge{
                      .predecessor = key.from
                    , .run_time    = run_time
                }
            );
            target.try_emplace(key.from);
        }
    }

}  // namespace

    [[nodiscard]] ResidualReverseGraph build_residual_reverse_graph(
          std::span<const RouteSegment>      route_segments
        , std::span<const ConnectionSegment> connection_segments
    ) {
        ResidualPhysicalEdgeTimes walk_edges;
        ResidualPhysicalEdgeTimes timed_edges;

        for (const auto& segment : route_segments) {
            if (is_walk(segment)) {
                retain_min_residual_edge_time(
                      walk_edges
                    , ResidualPhysicalEdgeKey{
                          .from = physical_from_key(segment)
                        , .to   = physical_to_key(segment)
                      }
                    , segment.run_time
                );
            }
        }

        for (const auto& segment : connection_segments) {
            if (!is_timed_connection(segment)
                || !segment.departure.has_value()
                || !segment.arrival.has_value()) {
                continue;
            }
            const auto& route_segment = route_segments[static_cast<std::size_t>(
                segment.route_segment.get()
            )];
            retain_min_residual_edge_time(
                  timed_edges
                , ResidualPhysicalEdgeKey{
                      .from = physical_from_key(route_segment)
                    , .to   = physical_to_key(route_segment)
                  }
                , Time{ segment.arrival->value() - segment.departure->value() }
            );
        }

        ResidualReverseGraph graph;
        append_residual_reverse_edges(graph.walk_predecessors_by_node, walk_edges);
        append_residual_reverse_edges(graph.timed_predecessors_by_node, timed_edges);
        return graph;
    }

}  // namespace timetable::domain::assignment
