#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/residual_reachability.hpp"

namespace timetable::domain::assignment {
namespace {

    RouteSegment access_route_segment() {
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

    RouteSegment timed_route_segment() {
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

    RouteSegment egress_route_segment() {
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

    std::vector<RouteSegment> residual_route_segments() {
        return {
              access_route_segment()
            , timed_route_segment()
            , egress_route_segment()
        };
    }

    std::vector<ConnectionSegment> residual_connection_segments() {
        return {
            ConnectionSegment{
                  .id            = ConnectionSegmentId{ 0 }
                , .route_segment = RouteSegmentId{ 1 }
                , .trip          = TripId{ 1 }
                , .from_index    = RoutePosition{ 0 }
                , .to_index      = RoutePosition{ 1 }
                , .departure     = Time{ 10.0 }
                , .arrival       = Time{ 20.0 }
            }
        };
    }

    std::vector<SearchCompletionTarget> residual_targets() {
        return {
            SearchCompletionTarget{
                  .index       = SearchCompletionTargetRef{ 0 }
                , .destination = ZoneId{ 2 }
            }
        };
    }

}  // namespace

TEST(ResidualReachability, BuildsRelaxedSuffixReachabilityAndLowerBounds) {
    const auto routes = residual_route_segments();
    const auto connections = residual_connection_segments();
    const auto targets = residual_targets();
    const auto reverse_graph = build_residual_reverse_graph(
          std::span<const RouteSegment>{ routes.data(), routes.size() }
        , std::span<const ConnectionSegment>{ connections.data(), connections.size() }
    );

    const auto reachability = build_residual_reachability(
          reverse_graph
        , std::span<const SearchCompletionTarget>{ targets.data(), targets.size() }
        , TransferCount{ 0 }
        , SearchImpedance{}
        , 1.0
    );
    ASSERT_TRUE(validate_residual_reachability(reachability, TransferCount{ 0 }).has_value());

    const auto decision = evaluate_residual_reachability(
          reachability
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(ZoneId{ 1 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::AtOrigin
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 0 }
    );
    EXPECT_TRUE(decision.feasible);

    const auto destination_it = reachability.destinations.find(ZoneId{ 2 });
    ASSERT_NE(destination_it, reachability.destinations.end());
    const auto origin_bounds = destination_it->second.suffix_lower_bounds.find(
        ResidualReachabilityKey{
              .current_physical    = endpoint_key(ZoneId{ 1 })
            , .phase               = SearchBranchPhase::AtOrigin
            , .remaining_transfers = TransferCount{ 0 }
        }
    );
    ASSERT_NE(origin_bounds, destination_it->second.suffix_lower_bounds.end());
    EXPECT_EQ(origin_bounds->second.journey_time, Time{ 20.0 });
    EXPECT_EQ(origin_bounds->second.transfers, TransferCount{ 0 });
}

TEST(ResidualReachability, ReportsTransferBudgetWhenOnlyLargerBudgetStateExists) {
    const auto routes = residual_route_segments();
    const auto connections = residual_connection_segments();
    const auto targets = residual_targets();
    const auto reverse_graph = build_residual_reverse_graph(
          std::span<const RouteSegment>{ routes.data(), routes.size() }
        , std::span<const ConnectionSegment>{ connections.data(), connections.size() }
    );

    const auto reachability = build_residual_reachability(
          reverse_graph
        , std::span<const SearchCompletionTarget>{ targets.data(), targets.size() }
        , TransferCount{ 1 }
        , SearchImpedance{}
        , 1.0
    );

    const auto decision = evaluate_residual_reachability(
          reachability
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(StopId{ 10 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::AfterTimedRide
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );

    EXPECT_FALSE(decision.feasible);
    EXPECT_EQ(decision.rejection_reason, ReachabilityRejectionReason::TransferBudget);
}

}  // namespace timetable::domain::assignment
