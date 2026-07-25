#include <utility>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/day_path/alternative.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] ConnectionLeg access_walk_leg(
        ZoneId origin
    ) {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::AccessWalk
            , .connection_segment = ConnectionSegmentId{ 10 }
            , .route_segment      = RouteSegmentId{ 20 }
            , .physical_from      = endpoint_key(origin)
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

    [[nodiscard]] ConnectionLeg egress_walk_leg(
        ZoneId destination
    ) {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::EgressWalk
            , .connection_segment = ConnectionSegmentId{ 12 }
            , .route_segment      = RouteSegmentId{ 22 }
            , .physical_from      = endpoint_key(StopId{ 200 })
            , .physical_to        = endpoint_key(destination)
            , .start_time         = Time{ 17.0 }
            , .end_time           = Time{ 20.0 }
            , .length             = Length{ 1.0 }
            , .fare               = 0.0
        };
    }

    [[nodiscard]] SearchConnection search_connection(
          ZoneId origin
        , ZoneId destination
    ) {
        auto connection = make_search_connection(
              origin
            , destination
            , ConnectionTrace{
                  .legs = {
                      access_walk_leg(origin)
                    , ride_leg()
                    , egress_walk_leg(destination)
                  }
              }
        );
        if (!connection) {
            ADD_FAILURE() << connection.error().to_string();
            return make_search_connection(
                  ZoneId{ 1 }
                , ZoneId{ 2 }
                , ConnectionTrace{
                      .legs = {
                          access_walk_leg(ZoneId{ 1 })
                        , ride_leg()
                        , egress_walk_leg(ZoneId{ 2 })
                      }
                  }
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

    [[nodiscard]] DayPathAlternative valid_alternative() {
        auto connection = search_connection(ZoneId{ 1 }, ZoneId{ 2 });
        return make_day_path_alternative_from_metrics(
              connection
            , day_path_signature_of(connection)
            , metrics(0.0, 20.0, 8.0, 1)
            , metrics_of(connection)
        );
    }

}  // namespace

TEST(DayPathAlternative, FactoryBuildsValidSingleSupportAlternative) {
    auto connection = search_connection(ZoneId{ 1 }, ZoneId{ 2 });
    const auto signature = day_path_signature_of(connection);

    const auto alternative = make_day_path_alternative_from_metrics(
          connection
        , signature
        , metrics(0.0, 20.0, 8.0, 1)
        , metrics_of(connection)
    );

    EXPECT_TRUE(validate_day_path_alternative(alternative, 0));
    EXPECT_EQ(alternative.identity.signature, signature);
    EXPECT_EQ(day_path_signature_of(alternative.support.representative), signature);
    ASSERT_EQ(alternative.support.split_support.supports.size(), 1u);
    EXPECT_EQ(alternative.support.split_support.supports[0].signature, signature);
    EXPECT_DOUBLE_EQ(alternative.support.representative_metrics.impedance, 8.0);
}

TEST(DayPathAlternative, ValidationRejectsEmptyTimedSupport) {
    auto alternative = valid_alternative();
    alternative.support.split_support.supports.clear();

    EXPECT_FALSE(validate_day_path_alternative(alternative, 0));
}

TEST(DayPathAlternative, ValidationRejectsRepresentativeSignatureMismatch) {
    auto connection = search_connection(ZoneId{ 1 }, ZoneId{ 2 });
    const auto mismatched_signature =
        day_path_signature_of(search_connection(ZoneId{ 1 }, ZoneId{ 3 }));

    const auto alternative = make_day_path_alternative_from_metrics(
          std::move(connection)
        , mismatched_signature
        , metrics(0.0, 20.0, 8.0, 1)
        , metrics_of(search_connection(ZoneId{ 1 }, ZoneId{ 2 }))
    );

    EXPECT_FALSE(validate_day_path_alternative(alternative, 0));
}

TEST(DayPathAlternative, ValidationRejectsTimedSupportSignatureMismatch) {
    auto alternative = valid_alternative();
    alternative.support.split_support.supports[0].signature =
        day_path_signature_of(search_connection(ZoneId{ 1 }, ZoneId{ 3 }));

    EXPECT_FALSE(validate_day_path_alternative(alternative, 0));
}

}  // namespace timetable::domain::assignment
