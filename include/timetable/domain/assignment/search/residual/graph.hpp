#pragma once

#include <span>
#include <unordered_map>
#include <vector>

#include "timetable/domain/assignment/search/residual/types.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    struct ResidualReverseEdge final {
        EndpointKey predecessor{};
        Time        run_time{};
    };

    struct ResidualReverseGraph final {
        std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> walk_predecessors_by_node{};
        std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> timed_predecessors_by_node{};
    };

    [[nodiscard]] ResidualReverseGraph build_residual_reverse_graph(
          std::span<const RouteSegment>      route_segments
        , std::span<const ConnectionSegment> connection_segments
    );

}  // namespace timetable::domain::assignment
