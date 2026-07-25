#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/day_path/support.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] ConnectionLeg access_walk_leg() {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::AccessWalk
            , .connection_segment = ConnectionSegmentId{ 10 }
            , .route_segment      = RouteSegmentId{ 20 }
            , .physical_from      = endpoint_key(ZoneId{ 1 })
            , .physical_to        = endpoint_key(StopId{ 100 })
            , .start_time         = Time{ 0.0 }
            , .end_time           = Time{ 5.0 }
            , .length             = Length{ 1.0 }
            , .fare               = 0.0
        };
    }

    [[nodiscard]] ConnectionLeg ride_leg() {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::Ride
            , .connection_segment = ConnectionSegmentId{ 11 }
            , .route_segment      = RouteSegmentId{ 21 }
            , .physical_from      = endpoint_key(StopId{ 100 })
            , .physical_to        = endpoint_key(StopId{ 200 })
            , .occurrence_from    = StopOccurrenceKey{
                  .stop     = StopId{ 100 }
                , .position = RoutePosition{ 2 }
              }
            , .occurrence_to      = StopOccurrenceKey{
                  .stop     = StopId{ 200 }
                , .position = RoutePosition{ 4 }
              }
            , .line               = LineId{ 30 }
            , .route              = RouteId{ 40 }
            , .trip               = TripId{ 50 }
            , .start_time         = Time{ 5.0 }
            , .end_time           = Time{ 17.0 }
            , .length             = Length{ 3.0 }
            , .fare               = 2.0
        };
    }

    [[nodiscard]] ConnectionLeg egress_walk_leg() {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::EgressWalk
            , .connection_segment = ConnectionSegmentId{ 12 }
            , .route_segment      = RouteSegmentId{ 22 }
            , .physical_from      = endpoint_key(StopId{ 200 })
            , .physical_to        = endpoint_key(ZoneId{ 2 })
            , .start_time         = Time{ 17.0 }
            , .end_time           = Time{ 20.0 }
            , .length             = Length{ 1.0 }
            , .fare               = 0.0
        };
    }

    [[nodiscard]] SearchConnection search_connection() {
        auto connection = make_search_connection(
              ZoneId{ 1 }
            , ZoneId{ 2 }
            , ConnectionTrace{
                  .legs = {
                      access_walk_leg()
                    , ride_leg()
                    , egress_walk_leg()
                  }
              }
        );
        if (!connection) {
            ADD_FAILURE() << connection.error().to_string();
            return make_search_connection(
                  ZoneId{ 1 }
                , ZoneId{ 2 }
                , ConnectionTrace{ .legs = { access_walk_leg(), ride_leg(), egress_walk_leg() } }
            ).value();
        }
        return *connection;
    }

    [[nodiscard]] CompleteConnectionMetrics metrics(
          double departure
        , double arrival
        , double impedance
        , int    transfers
    ) {
        return CompleteConnectionMetrics{
              .departure    = Time{ departure }
            , .arrival      = Time{ arrival }
            , .journey_time = Time{ arrival - departure }
            , .transfers    = TransferCount{ transfers }
            , .impedance    = impedance
        };
    }

    [[nodiscard]] DayPathSupportDescriptor support_descriptor(
          DayPathSignature          signature
        , CompleteConnectionMetrics complete_metrics
    ) {
        return DayPathSupportDescriptor{
              .signature        = std::move(signature)
            , .complete_metrics = complete_metrics
        };
    }

    [[nodiscard]] DayPathAlternative alternative_with_support_metrics(
        std::vector<CompleteConnectionMetrics> support_metrics
    ) {
        auto connection = search_connection();
        const auto signature = day_path_signature_of(connection);
        std::vector<DayPathSupportDescriptor> supports;
        supports.reserve(support_metrics.size());
        for (const auto metric : support_metrics) {
            supports.push_back(support_descriptor(signature, metric));
        }

        return DayPathAlternative{
              .identity = DayPathIdentity{ .signature = signature }
            , .support = DayPathTimedSupport{
                  .representative         = std::move(connection)
                , .representative_metrics = metrics(0.0, 20.0, 10.0, 0)
                , .split_support          = DayPathSplitSupport{
                      .supports = std::move(supports)
                  }
              }
        };
    }

}  // namespace

TEST(DayPathSupport, DescriptorProjectsOnlyRideLegsToTimedSupport) {
    const auto connection = search_connection();
    const auto signature = day_path_signature_of(connection);

    const auto descriptor = make_day_path_support_descriptor(
          connection
        , signature
        , metrics(0.0, 20.0, 10.0, 0)
        , metrics_of(connection)
    );

    EXPECT_EQ(descriptor.signature, signature);
    ASSERT_EQ(descriptor.ride_legs.size(), 1u);
    EXPECT_EQ(descriptor.ride_legs[0].connection_segment, ConnectionSegmentId{ 11 });
    EXPECT_EQ(descriptor.ride_legs[0].route_segment, RouteSegmentId{ 21 });
    EXPECT_EQ(descriptor.ride_legs[0].line, LineId{ 30 });
    EXPECT_EQ(descriptor.ride_legs[0].route, RouteId{ 40 });
    EXPECT_EQ(descriptor.ride_legs[0].trip, TripId{ 50 });
    EXPECT_EQ(descriptor.ride_legs[0].from_index, RoutePosition{ 2 });
    EXPECT_EQ(descriptor.ride_legs[0].to_index, RoutePosition{ 4 });
}

TEST(DayPathSupport, MetricSetFallsBackToRepresentativeWhenSplitSupportIsEmpty) {
    const auto alternative = alternative_with_support_metrics({});

    const auto metric_set = day_path_support_metric_set(alternative);

    ASSERT_EQ(metric_set.size(), 1u);
    EXPECT_DOUBLE_EQ(metric_set[0].impedance, 10.0);
}

TEST(DayPathSupport, MetricSetUsesAllTimedSupportsWhenPresent) {
    const auto alternative = alternative_with_support_metrics({
          metrics(10.0, 20.0, 5.0, 0)
        , metrics(11.0, 21.0, 6.0, 1)
    });

    const auto metric_set = day_path_support_metric_set(alternative);

    ASSERT_EQ(metric_set.size(), 2u);
    EXPECT_DOUBLE_EQ(metric_set[0].impedance, 5.0);
    EXPECT_DOUBLE_EQ(metric_set[1].impedance, 6.0);
}

TEST(DayPathSupport, SupportSetDominatesWhenEveryRightSupportIsDominated) {
    const auto dominant = alternative_with_support_metrics({
        metrics(10.0, 20.0, 5.0, 0)
    });
    const auto dominated = alternative_with_support_metrics({
        metrics(9.0, 21.0, 6.0, 1)
    });

    EXPECT_TRUE(day_path_support_set_dominates(dominant, dominated));
    EXPECT_FALSE(day_path_support_set_dominates(dominated, dominant));
}

}  // namespace timetable::domain::assignment
