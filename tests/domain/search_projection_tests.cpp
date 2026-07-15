#include <vector>

#include <gtest/gtest.h>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/projection/complete_connection.hpp"
#include "timetable/domain/assignment/search/projection.hpp"

namespace timetable::domain::assignment {
namespace {

    using test_support::completion_target;
    using test_support::domain;
    using test_support::task;

    RouteSegment complete_access_route_segment() {
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

    RouteSegment complete_timed_route_segment() {
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

    RouteSegment complete_egress_route_segment() {
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

    PreprocessedNetwork complete_projection_network() {
        return PreprocessedNetwork{
              .route_segments = {
                    complete_access_route_segment()
                  , complete_timed_route_segment()
                  , complete_egress_route_segment()
                }
            , .connection_segments = {
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
                }
        };
    }

    SearchBranch complete_projection_branch() {
        auto support = OdDaySupportPrefix{};
        support = append_od_day_support_segment(support, ConnectionSegmentId{ 0 });
        support = append_od_day_support_segment(support, ConnectionSegmentId{ 1 });
        support = append_od_day_support_segment(support, ConnectionSegmentId{ 2 });

        return SearchBranch{
              .trace = SearchPartialTrace{
                    .origin           = ZoneId{ 1 }
                  , .current_physical = endpoint_key(ZoneId{ 2 })
                  , .phase            = SearchBranchPhase::Completed
                  , .incoming_segment = ConnectionSegmentId{ 2 }
              }
            , .metrics = SearchPartialMetrics{
                    .departure       = Time{ 5.0 }
                  , .current_time    = Time{ 25.0 }
                  , .access_time     = Time{ 5.0 }
                  , .in_vehicle_time = Time{ 10.0 }
                  , .egress_time     = Time{ 5.0 }
            }
            , .od_day_carrier = OdDayProductionCarrier{
                  .path_identity    = make_od_day_path_prefix(ZoneId{ 1 })
                , .support_envelope = TimedSupportEnvelope{}
                , .support_prefix   = std::move(support)
              }
        };
    }

}  // namespace

TEST(SearchProjectionSlot, DemandTaskSlotCarriesOptionalTaskReference) {
    const auto demand_task = task(7, 1, 2, 3, 100.0, 200.0);

    const auto slot = make_demand_task_projection_slot(demand_task, 11u);

    EXPECT_EQ(slot.kind, SearchProjectionSlotKind::DemandTask);
    ASSERT_TRUE(slot.task.has_value());
    EXPECT_EQ(&slot.task->get(), &demand_task);
    ASSERT_TRUE(slot.task_ref.has_value());
    EXPECT_EQ(*slot.task_ref, demand_task.index);
    ASSERT_TRUE(slot.result_index.has_value());
    EXPECT_EQ(*slot.result_index, 11u);
}

TEST(SearchProjectionSlot, CompletionTargetSlotsCarryOnlyTargetIdentity) {
    const SearchTreeJob job{
          .index              = SearchTreeJobRef{ 0 }
        , .origin             = ZoneId{ 1 }
        , .departure_domain   = domain(0.0, 500.0)
        , .completion_targets = {
              completion_target(0, 2),
              completion_target(1, 3)
          }
        , .projection_tasks   = { SearchTaskRef{ 7 } }
    };

    const auto slots = build_completion_target_projection_slots(job);

    ASSERT_EQ(slots.size(), 2u);
    EXPECT_EQ(slots[0].kind, SearchProjectionSlotKind::CompletionTarget);
    EXPECT_EQ(slots[0].origin, ZoneId{ 1 });
    EXPECT_EQ(slots[0].destination, ZoneId{ 2 });
    EXPECT_FALSE(slots[0].task_ref.has_value());
    EXPECT_FALSE(slots[0].task.has_value());
    ASSERT_TRUE(slots[0].completion_target.has_value());
    EXPECT_EQ(*slots[0].completion_target, SearchCompletionTargetRef{ 0 });
}

TEST(SearchProjectionSlot, OdDayPairSlotsAreDayLevelPairProjections) {
    const SearchTreeJob job{
          .index              = SearchTreeJobRef{ 0 }
        , .origin             = ZoneId{ 1 }
        , .departure_domain   = domain(0.0, 500.0)
        , .completion_targets = {
              completion_target(0, 2)
          }
        , .projection_tasks   = { SearchTaskRef{ 7 } }
    };

    const auto slots = build_od_day_pair_projection_slots(job);

    ASSERT_EQ(slots.size(), 1u);
    EXPECT_EQ(slots[0].kind, SearchProjectionSlotKind::OdDayPair);
    EXPECT_EQ(slots[0].origin, ZoneId{ 1 });
    EXPECT_EQ(slots[0].destination, ZoneId{ 2 });
    EXPECT_FALSE(slots[0].interval.has_value());
    EXPECT_FALSE(slots[0].task.has_value());
}

TEST(SearchProjectionSlot, OdDayPostLayerRetentionRejectsRawCompleteAlternatives) {
    const SearchProjectionSlot slot{
          .kind        = SearchProjectionSlotKind::OdDayPair
        , .origin      = ZoneId{ 1 }
        , .destination = ZoneId{ 2 }
    };
    SearchProjectionRetention retention{
        .slot = slot
    };
    retention.compact_complete_connections.metrics.push_back(CompleteConnectionMetrics{});

    EXPECT_FALSE(validate_od_day_post_layer_retention(slot, retention).has_value());
}

TEST(SearchProjectionSlot, OdDayPostLayerResultRejectsRawConnections) {
    const SearchProjectionSlot slot{
          .kind        = SearchProjectionSlotKind::OdDayPair
        , .origin      = ZoneId{ 1 }
        , .destination = ZoneId{ 2 }
    };
    SearchSlotResult result{
          .slot             = slot
        , .connection_count = 1u
    };

    EXPECT_FALSE(validate_od_day_post_layer_result(result).has_value());
}

TEST(SearchCompleteProjection, MaterializesCanonicalConnectionFromBranchSupportPrefix) {
    const auto network = complete_projection_network();
    BranchArena branches;
    const auto branch = complete_projection_branch();

    auto connection = complete_connection(
          branches
        , branch
        , network
        , TransferLimits{ .allow_end_wait = true }
        , ZoneId{ 2 }
    );

    ASSERT_TRUE(connection.has_value()) << connection.error().message();
    ASSERT_TRUE(connection->has_value());
    const auto& trace = canonical_connection(**connection).trace;
    ASSERT_EQ(trace.legs.size(), 3u);
    EXPECT_EQ(trace.legs[0].kind, ConnectionLegKind::AccessWalk);
    EXPECT_EQ(trace.legs[1].kind, ConnectionLegKind::Ride);
    EXPECT_EQ(trace.legs[2].kind, ConnectionLegKind::EgressWalk);
    EXPECT_EQ(trace.legs[0].start_time, Time{ 5.0 });
    EXPECT_EQ(trace.legs[2].end_time, Time{ 25.0 });
}

TEST(SearchCompleteProjection, CompactRetentionRejectsDuplicateSegmentTrace) {
    BranchArena branches;
    const auto branch = complete_projection_branch();
    CompactCompleteConnectionRetention retention;
    const auto metrics = complete_connection_metrics_from_branch(
          branch
        , SearchCostContext{}
    );
    ASSERT_TRUE(metrics.has_value()) << metrics.error().message();

    const auto first = retain_exact_compact_complete_connection(
          retention
        , branches
        , branch
        , *metrics
        , CompleteConnectionDominanceConfig{}
    );
    ASSERT_TRUE(first.has_value()) << first.error().message();
    EXPECT_TRUE(first->accepted);

    const auto duplicate = retain_exact_compact_complete_connection(
          retention
        , branches
        , branch
        , *metrics
        , CompleteConnectionDominanceConfig{}
    );
    ASSERT_TRUE(duplicate.has_value()) << duplicate.error().message();
    EXPECT_FALSE(duplicate->accepted);
    EXPECT_EQ(retention.traces.size(), 1u);
    EXPECT_EQ(retention.metrics.size(), 1u);
}

TEST(SearchCompleteProjection, SlotRetentionReturnsDiagnosticsAsValues) {
    const auto network = complete_projection_network();
    BranchArena branches;
    const auto branch = complete_projection_branch();
    const SearchProjectionSlot slot{
          .kind        = SearchProjectionSlotKind::CompletionTarget
        , .origin      = ZoneId{ 1 }
        , .destination = ZoneId{ 2 }
    };
    SearchProjectionRetention retention{
        .slot = slot
    };

    auto first = retain_complete_projection_for_slot(
          branch
        , branches
        , network
        , TransferLimits{ .allow_end_wait = true }
        , SearchCostContext{}
        , slot
        , AssignmentPeriodConfig{}
        , ConnectionAdmissibilityConfig{}
        , CompleteConnectionDominanceConfig{}
        , retention
    );
    ASSERT_TRUE(first.has_value()) << first.error().message();
    EXPECT_EQ(first->completed_connections, 1u);
    EXPECT_EQ(first->rejected_complete_dominance, 0u);
    EXPECT_EQ(retention.compact_complete_connections.metrics.size(), 1u);

    auto duplicate = retain_complete_projection_for_slot(
          branch
        , branches
        , network
        , TransferLimits{ .allow_end_wait = true }
        , SearchCostContext{}
        , slot
        , AssignmentPeriodConfig{}
        , ConnectionAdmissibilityConfig{}
        , CompleteConnectionDominanceConfig{}
        , retention
    );
    ASSERT_TRUE(duplicate.has_value()) << duplicate.error().message();
    EXPECT_EQ(duplicate->completed_connections, 1u);
    EXPECT_EQ(duplicate->rejected_complete_dominance, 1u);
    EXPECT_EQ(retention.compact_complete_connections.metrics.size(), 1u);
}

}  // namespace timetable::domain::assignment
