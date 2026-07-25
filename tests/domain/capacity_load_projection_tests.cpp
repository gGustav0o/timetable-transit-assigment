#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/capacity/load_projection.hpp"
#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemKey item_key(
        std::int64_t from_index
    ) {
        return VehicleJourneyItemKey{
              .trip = TripId{ 9 }
            , .from_index = RoutePosition{ from_index }
        };
    }

    [[nodiscard]] VehicleJourneyItemLoadKey load_key(
        std::int64_t from_index
    ) {
        return VehicleJourneyItemLoadKey{
              .interval = IntervalId{ 2 }
            , .item = item_key(from_index)
        };
    }

    [[nodiscard]] VehicleJourneyItemLoad load(
          std::int64_t from_index
        , double       passengers
    ) {
        return VehicleJourneyItemLoad{
              .key = load_key(from_index)
            , .passengers = passengers
        };
    }

    [[nodiscard]] DayPathSignature day_path_signature() {
        return DayPathSignature{
              .origin = ZoneId{ 1 }
            , .destination = ZoneId{ 3 }
            , .legs = {}
        };
    }

    [[nodiscard]] DayPathRideSupportLeg ride_support_leg(
          std::int64_t from_index
        , std::int64_t to_index
    ) {
        return DayPathRideSupportLeg{
              .connection_segment = ConnectionSegmentId{ 1 }
            , .route_segment = RouteSegmentId{ 1 }
            , .line = LineId{ 1 }
            , .route = RouteId{ 1 }
            , .trip = TripId{ 9 }
            , .occurrence_from = StopOccurrenceKey{
                  .stop = StopId{ 10 }
                , .position = RoutePosition{ from_index }
              }
            , .occurrence_to = StopOccurrenceKey{
                  .stop = StopId{ 11 }
                , .position = RoutePosition{ to_index }
              }
            , .from_index = RoutePosition{ from_index }
            , .to_index = RoutePosition{ to_index }
            , .departure = Time{ 10.0 }
            , .arrival = Time{ 20.0 }
        };
    }

    [[nodiscard]] mathfp::Expected<SearchConnection> placeholder_connection() {
        return make_search_connection(
              ZoneId{ 1 }
            , ZoneId{ 3 }
            , ConnectionTrace{
                  .legs = {
                      ConnectionLeg{
                            .kind = ConnectionLegKind::AccessWalk
                          , .connection_segment = ConnectionSegmentId{ 100 }
                          , .route_segment = RouteSegmentId{ 100 }
                          , .physical_from = endpoint_key(ZoneId{ 1 })
                          , .physical_to = endpoint_key(ZoneId{ 3 })
                          , .occurrence_from = std::nullopt
                          , .occurrence_to = std::nullopt
                          , .line = std::nullopt
                          , .route = std::nullopt
                          , .trip = std::nullopt
                          , .start_time = Time{ 0.0 }
                          , .end_time = Time{ 1.0 }
                          , .length = Length{ 1.0 }
                          , .fare = 0.0
                      }
                  }
              }
        );
    }

    [[nodiscard]] ConnectionDemandShare day_path_share(
          double                   passengers
        , DayPathSupportDescriptor support
        , SearchConnection         connection
    ) {
        const auto signature = support.signature;
        return ConnectionDemandShare{
              .origin = signature.origin
            , .destination = signature.destination
            , .interval = IntervalId{ 2 }
            , .source = DemandShareAlternativeSource::DayPath
            , .day_path = signature
            , .connection = std::move(connection)
            , .day_path_support = std::move(support)
            , .passengers = passengers
            , .probability = 1.0
            , .independence = 1.0
            , .split_impedance = 1.0
        };
    }

    [[nodiscard]] DayPathSupportDescriptor support_descriptor(
        std::vector<DayPathRideSupportLeg> ride_legs
    ) {
        return DayPathSupportDescriptor{
              .signature = day_path_signature()
            , .complete_metrics = CompleteConnectionMetrics{}
            , .connection_metrics = ConnectionMetrics{}
            , .ride_legs = std::move(ride_legs)
        };
    }

}  // namespace

TEST(CapacityLoadProjection, OccupiedItemsUseHalfOpenRouteInterval) {
    const auto result = vehicle_journey_items_occupied(
          TripId{ 9 }
        , RoutePosition{ 2 }
        , RoutePosition{ 5 }
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->size(), 3u);
    EXPECT_EQ((*result)[0], item_key(2));
    EXPECT_EQ((*result)[1], item_key(3));
    EXPECT_EQ((*result)[2], item_key(4));
}

TEST(CapacityLoadProjection, RejectsInvalidOccupancyAndLoadRows) {
    EXPECT_FALSE(vehicle_journey_items_occupied(
          TripId{ 9 }
        , RoutePosition{ 5 }
        , RoutePosition{ 5 }
    ).has_value());
    EXPECT_FALSE(validate_vehicle_journey_item_load(
        load(2, std::numeric_limits<double>::quiet_NaN())
    ).has_value());
    EXPECT_FALSE(validate_vehicle_journey_item_loads(
        VehicleJourneyItemLoads{
            .items = {
                  load(2, 1.0)
                , load(2, 2.0)
            }
        }
    ).has_value());
}

TEST(CapacityLoadProjection, DayPathSupportProjectsPassengersToEveryOccupiedItem) {
    const auto connection = placeholder_connection();
    ASSERT_TRUE(connection.has_value()) << connection.error().to_string();

    DemandSplitResult split;
    split.shares.push_back(
        day_path_share(
              7.0
            , support_descriptor({
                  ride_support_leg(2, 5)
              })
            , *connection
        )
    );

    const auto result = build_day_path_elementary_segment_loads(split);

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->items.size(), 3u);
    EXPECT_EQ(result->items[0].key, load_key(2));
    EXPECT_EQ(result->items[1].key, load_key(3));
    EXPECT_EQ(result->items[2].key, load_key(4));
    EXPECT_DOUBLE_EQ(result->items[0].passengers, 7.0);
    EXPECT_DOUBLE_EQ(result->items[1].passengers, 7.0);
    EXPECT_DOUBLE_EQ(result->items[2].passengers, 7.0);
}

TEST(CapacityLoadProjection, AccumulatorCombinesRepeatedLoadsDeterministically) {
    ElementarySegmentLoadAccumulator accumulator;
    const auto accumulated = accumulate_elementary_segment_loads(
          accumulator
        , ElementarySegmentLoads{
              .items = {
                    load(2, 3.0)
                  , load(3, 4.0)
              }
          }
    );
    ASSERT_TRUE(accumulated.has_value()) << accumulated.error().to_string();

    const auto again = accumulate_elementary_segment_loads(
          accumulator
        , ElementarySegmentLoads{
              .items = {
                    load(2, 5.0)
              }
          }
    );
    ASSERT_TRUE(again.has_value()) << again.error().to_string();

    const auto result = materialize_elementary_segment_loads(accumulator);

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->items.size(), 2u);
    EXPECT_EQ(result->items[0].key, load_key(2));
    EXPECT_EQ(result->items[1].key, load_key(3));
    EXPECT_DOUBLE_EQ(result->items[0].passengers, 8.0);
    EXPECT_DOUBLE_EQ(result->items[1].passengers, 4.0);
}

}  // namespace timetable::domain::assignment
