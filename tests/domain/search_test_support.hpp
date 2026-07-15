#pragma once

#include <cstdint>
#include <vector>

#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/problem.hpp"

namespace timetable::domain::assignment::test_support {

    inline SearchTimeDomain domain(double begin, double end) {
        return SearchTimeDomain{
            .windows = {
                SearchTimeWindow{
                      .begin = Time{ begin }
                    , .end   = Time{ end }
                }
            }
        };
    }

    inline TimeInterval interval(std::int64_t id, double start, double end) {
        return TimeInterval{
              .id    = IntervalId{ id }
            , .start = Time{ start }
            , .end   = Time{ end }
        };
    }

    inline SearchTask task(
          std::int64_t index
        , std::int64_t origin
        , std::int64_t destination
        , std::int64_t interval_id
        , double       window_begin
        , double       window_end
    ) {
        return SearchTask{
              .index            = SearchTaskRef{ index }
            , .origin           = ZoneId{ origin }
            , .destination      = ZoneId{ destination }
            , .interval         = interval(interval_id, window_begin, window_end)
            , .departure_domain = domain(window_begin, window_end)
        };
    }

    inline SearchCompletionTarget completion_target(
          std::int64_t index
        , std::int64_t destination
    ) {
        return SearchCompletionTarget{
              .index       = SearchCompletionTargetRef{ index }
            , .destination = ZoneId{ destination }
        };
    }

    inline RouteSegment od_day_access_route_segment() {
        return RouteSegment{
              .id       = RouteSegmentId{ 0 }
            , .length   = Length{ 1.0 }
            , .run_time = Time{ 5.0 }
            , .topology = WalkRouteTopology{
                  .from = ZoneId{ 1 }
                , .to   = StopId{ 10 }
              }
        };
    }

    inline RouteSegment od_day_ride_route_segment() {
        return RouteSegment{
              .id       = RouteSegmentId{ 1 }
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

    inline RouteSegment od_day_egress_route_segment() {
        return RouteSegment{
              .id       = RouteSegmentId{ 2 }
            , .length   = Length{ 1.0 }
            , .run_time = Time{ 5.0 }
            , .topology = WalkRouteTopology{
                  .from = StopId{ 11 }
                , .to   = ZoneId{ 2 }
              }
        };
    }

    inline std::vector<RouteSegment> od_day_route_segments() {
        return {
              od_day_access_route_segment()
            , od_day_ride_route_segment()
            , od_day_egress_route_segment()
        };
    }

    inline std::vector<ConnectionSegment> od_day_connection_segments() {
        return {
              ConnectionSegment{
                    .id            = ConnectionSegmentId{ 0 }
                  , .route_segment = RouteSegmentId{ 0 }
              }
            , ConnectionSegment{
                    .id            = ConnectionSegmentId{ 1 }
                  , .route_segment = RouteSegmentId{ 1 }
                  , .trip          = TripId{ 1 }
                  , .from_index    = RoutePosition{ 0 }
                  , .to_index      = RoutePosition{ 1 }
                  , .departure     = Time{ 10.0 }
                  , .arrival       = Time{ 20.0 }
              }
            , ConnectionSegment{
                    .id            = ConnectionSegmentId{ 2 }
                  , .route_segment = RouteSegmentId{ 2 }
              }
        };
    }

    inline mathfp::Expected<PreprocessedNetwork> od_day_preprocessed_network() {
        return build_preprocessed_network_from_segments(
              od_day_route_segments()
            , od_day_connection_segments()
        );
    }

}  // namespace timetable::domain::assignment::test_support
