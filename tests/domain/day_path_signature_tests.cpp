#include <gtest/gtest.h>

#include "timetable/domain/assignment/day_path/signature.hpp"
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

    [[nodiscard]] ConnectionLeg initial_wait_leg() {
        return ConnectionLeg{
              .kind          = ConnectionLegKind::InitialWait
            , .physical_from = endpoint_key(StopId{ 100 })
            , .physical_to   = endpoint_key(StopId{ 100 })
            , .start_time    = Time{ 5.0 }
            , .end_time      = Time{ 7.0 }
            , .length        = Length{ 0.0 }
            , .fare          = 0.0
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
                , .position = RoutePosition{ 3 }
              }
            , .line               = LineId{ 30 }
            , .route              = RouteId{ 40 }
            , .trip               = TripId{ 50 }
            , .start_time         = Time{ 7.0 }
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

}  // namespace

TEST(DayPathSignature, ProductionLegDropsTimedAndSupplyIdentity) {
    const auto leg = production_day_path_leg(DayPathLeg{
          .kind            = ConnectionLegKind::Ride
        , .route_segment   = RouteSegmentId{ 21 }
        , .physical_from   = endpoint_key(StopId{ 100 })
        , .physical_to     = endpoint_key(StopId{ 200 })
        , .occurrence_from = StopOccurrenceKey{
              .stop     = StopId{ 100 }
            , .position = RoutePosition{ 2 }
          }
        , .occurrence_to   = StopOccurrenceKey{
              .stop     = StopId{ 200 }
            , .position = RoutePosition{ 3 }
          }
        , .line            = LineId{ 30 }
        , .route           = RouteId{ 40 }
    });

    EXPECT_FALSE(leg.route_segment.has_value());
    EXPECT_FALSE(leg.occurrence_from.has_value());
    EXPECT_FALSE(leg.occurrence_to.has_value());
    EXPECT_EQ(leg.line, LineId{ 30 });
    EXPECT_EQ(leg.route, RouteId{ 40 });
}

TEST(DayPathSignature, ProductionNonRideLegDropsLineAndRoute) {
    const auto leg = production_day_path_leg(DayPathLeg{
          .kind          = ConnectionLegKind::AccessWalk
        , .route_segment = RouteSegmentId{ 20 }
        , .physical_from = endpoint_key(ZoneId{ 1 })
        , .physical_to   = endpoint_key(StopId{ 100 })
        , .line          = LineId{ 30 }
        , .route         = RouteId{ 40 }
    });

    EXPECT_FALSE(leg.route_segment.has_value());
    EXPECT_FALSE(leg.line.has_value());
    EXPECT_FALSE(leg.route.has_value());
}

TEST(DayPathSignature, PrefixAppendKeepsEarlierPrefixReusable) {
    const auto origin = ZoneId{ 1 };
    const auto initial = make_day_path_prefix(origin);
    const auto first = append_day_path_leg(
          initial
        , day_path_leg_of(access_walk_leg())
    );
    const auto second = append_day_path_leg(
          first
        , day_path_leg_of(ride_leg())
    );

    EXPECT_TRUE(initial.legs.empty());
    ASSERT_EQ(first.legs.size(), 1u);
    ASSERT_EQ(second.legs.size(), 2u);
    EXPECT_EQ(first.legs[0].kind, ConnectionLegKind::AccessWalk);
    EXPECT_EQ(second.legs[0].kind, ConnectionLegKind::AccessWalk);
    EXPECT_EQ(second.legs[1].kind, ConnectionLegKind::Ride);
}

TEST(DayPathSignature, SearchConnectionSignatureSkipsWaitLegsAndKeepsProductionCarrier) {
    auto connection = make_search_connection(
          ZoneId{ 1 }
        , ZoneId{ 2 }
        , ConnectionTrace{
              .legs = {
                  access_walk_leg()
                , initial_wait_leg()
                , ride_leg()
                , egress_walk_leg()
              }
          }
    );
    ASSERT_TRUE(connection.has_value()) << connection.error().to_string();

    const auto signature = day_path_signature_of(*connection);

    EXPECT_EQ(signature.origin, ZoneId{ 1 });
    EXPECT_EQ(signature.destination, ZoneId{ 2 });
    ASSERT_EQ(signature.legs.size(), 3u);
    EXPECT_EQ(signature.legs[0].kind, ConnectionLegKind::AccessWalk);
    EXPECT_EQ(signature.legs[1].kind, ConnectionLegKind::Ride);
    EXPECT_EQ(signature.legs[1].line, LineId{ 30 });
    EXPECT_EQ(signature.legs[1].route, RouteId{ 40 });
    EXPECT_FALSE(signature.legs[1].route_segment.has_value());
    EXPECT_FALSE(signature.legs[1].occurrence_from.has_value());
    EXPECT_FALSE(signature.legs[1].occurrence_to.has_value());
    EXPECT_EQ(signature.legs[2].kind, ConnectionLegKind::EgressWalk);
}

}  // namespace timetable::domain::assignment
