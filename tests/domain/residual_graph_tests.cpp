#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/residual/graph.hpp"

namespace timetable::domain::assignment {
namespace {

    RouteSegment walk_segment(
          std::int64_t id
        , double       run_time
    ) {
        return RouteSegment{
              .id       = RouteSegmentId{ id }
            , .length   = Length{ 1.0 }
            , .run_time = Time{ run_time }
            , .topology = WalkRouteTopology{
                  .from = ZoneId{ 1 }
                , .to   = StopId{ 10 }
              }
        };
    }

    RouteSegment timed_route_segment(
        std::int64_t id
    ) {
        return RouteSegment{
              .id       = RouteSegmentId{ id }
            , .length   = Length{ 3.0 }
            , .run_time = Time{ 10.0 }
            , .topology = LineRouteTopology{
                  .from  = StopOccurrence{ .stop = StopId{ 10 }, .position = RoutePosition{ 0 } }
                , .to    = StopOccurrence{ .stop = StopId{ 11 }, .position = RoutePosition{ 1 } }
                , .line  = LineId{ 1 }
                , .route = RouteId{ 1 }
              }
        };
    }

    ConnectionSegment timed_connection(
          std::int64_t id
        , double       departure
        , double       arrival
    ) {
        return ConnectionSegment{
              .id            = ConnectionSegmentId{ id }
            , .route_segment = RouteSegmentId{ 2 }
            , .trip          = TripId{ id }
            , .from_index    = RoutePosition{ 0 }
            , .to_index      = RoutePosition{ 1 }
            , .departure     = Time{ departure }
            , .arrival       = Time{ arrival }
        };
    }

}  // namespace

TEST(ResidualGraph, KeepsMinimumRunTimePerPhysicalReverseEdge) {
    const auto routes = std::vector<RouteSegment>{
          walk_segment(0, 7.0)
        , walk_segment(1, 5.0)
        , timed_route_segment(2)
    };
    const auto connections = std::vector<ConnectionSegment>{
          timed_connection(0, 10.0, 21.0)
        , timed_connection(1, 12.0, 19.0)
        , ConnectionSegment{
              .id            = ConnectionSegmentId{ 2 }
            , .route_segment = RouteSegmentId{ 2 }
            , .trip          = TripId{ 2 }
            , .from_index    = RoutePosition{ 0 }
            , .to_index      = RoutePosition{ 1 }
          }
    };

    const auto graph = build_residual_reverse_graph(
          std::span<const RouteSegment>{ routes.data(), routes.size() }
        , std::span<const ConnectionSegment>{ connections.data(), connections.size() }
    );

    const auto walk_target = graph.walk_predecessors_by_node.find(endpoint_key(StopId{ 10 }));
    ASSERT_NE(walk_target, graph.walk_predecessors_by_node.end());
    ASSERT_EQ(walk_target->second.size(), 1u);
    EXPECT_EQ(walk_target->second.front().predecessor, endpoint_key(ZoneId{ 1 }));
    EXPECT_EQ(walk_target->second.front().run_time, Time{ 5.0 });

    const auto timed_target = graph.timed_predecessors_by_node.find(endpoint_key(StopId{ 11 }));
    ASSERT_NE(timed_target, graph.timed_predecessors_by_node.end());
    ASSERT_EQ(timed_target->second.size(), 1u);
    EXPECT_EQ(timed_target->second.front().predecessor, endpoint_key(StopId{ 10 }));
    EXPECT_EQ(timed_target->second.front().run_time, Time{ 7.0 });
}

}  // namespace timetable::domain::assignment
