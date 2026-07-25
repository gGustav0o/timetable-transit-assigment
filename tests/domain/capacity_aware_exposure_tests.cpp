#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/capacity_aware/exposure.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemKey item_key(
        std::int64_t from_index
    ) {
        return VehicleJourneyItemKey{
              .trip = TripId{ 11 }
            , .from_index = RoutePosition{ from_index }
        };
    }

    [[nodiscard]] VehicleJourneyItemCapacity capacity(
          std::int64_t from_index
        , double       total_capacity = 100.0
    ) {
        return VehicleJourneyItemCapacity{
              .key = item_key(from_index)
            , .total_capacity = total_capacity
            , .seat_capacity = total_capacity
        };
    }

    [[nodiscard]] VehicleJourneyItemLoad load(
          std::int64_t from_index
        , double       passengers
        , IntervalId   interval = IntervalId{ 3 }
    ) {
        return VehicleJourneyItemLoad{
              .key = VehicleJourneyItemLoadKey{
                    .interval = interval
                  , .item = item_key(from_index)
              }
            , .passengers = passengers
        };
    }

    [[nodiscard]] VehicleJourneyItemLoadState load_state(
        std::vector<VehicleJourneyItemLoad> items
    ) {
        return VehicleJourneyItemLoadState{
            .items = std::move(items)
        };
    }

    [[nodiscard]] VehicleJourneyItemCapacitySet capacity_set(
        std::vector<VehicleJourneyItemCapacity> items
    ) {
        return VehicleJourneyItemCapacitySet{
            .items = std::move(items)
        };
    }

    [[nodiscard]] ConnectionLeg ride_leg(
          std::int64_t from_index
        , std::int64_t to_index
        , double       start_time = 10.0
        , double       end_time = 30.0
    ) {
        return ConnectionLeg{
              .kind = ConnectionLegKind::Ride
            , .connection_segment = ConnectionSegmentId{ 40 }
            , .route_segment = RouteSegmentId{ 50 }
            , .physical_from = endpoint_key(StopId{ 100 + from_index })
            , .physical_to = endpoint_key(StopId{ 100 + to_index })
            , .occurrence_from = StopOccurrenceKey{
                  .stop = StopId{ 100 + from_index }
                , .position = RoutePosition{ from_index }
              }
            , .occurrence_to = StopOccurrenceKey{
                  .stop = StopId{ 100 + to_index }
                , .position = RoutePosition{ to_index }
              }
            , .line = LineId{ 7 }
            , .route = RouteId{ 8 }
            , .trip = TripId{ 11 }
            , .start_time = Time{ start_time }
            , .end_time = Time{ end_time }
            , .length = Length{ 2.0 }
            , .fare = 0.0
        };
    }

    [[nodiscard]] Connection ride_connection(
        ConnectionLeg leg
    ) {
        return Connection{
              .origin = ZoneId{ 1 }
            , .destination = ZoneId{ 2 }
            , .trace = ConnectionTrace{
                  .legs = {
                      std::move(leg)
                  }
              }
        };
    }

    [[nodiscard]] DayPathSignature day_path_signature() {
        return DayPathSignature{
              .origin = ZoneId{ 1 }
            , .destination = ZoneId{ 2 }
            , .legs = {}
        };
    }

    [[nodiscard]] DayPathRideSupportLeg support_leg(
          std::int64_t from_index
        , std::int64_t to_index
        , double       departure = 10.0
        , double       arrival = 30.0
    ) {
        return DayPathRideSupportLeg{
              .connection_segment = ConnectionSegmentId{ 40 }
            , .route_segment = RouteSegmentId{ 50 }
            , .line = LineId{ 7 }
            , .route = RouteId{ 8 }
            , .trip = TripId{ 11 }
            , .occurrence_from = StopOccurrenceKey{
                  .stop = StopId{ 100 + from_index }
                , .position = RoutePosition{ from_index }
              }
            , .occurrence_to = StopOccurrenceKey{
                  .stop = StopId{ 100 + to_index }
                , .position = RoutePosition{ to_index }
              }
            , .from_index = RoutePosition{ from_index }
            , .to_index = RoutePosition{ to_index }
            , .departure = Time{ departure }
            , .arrival = Time{ arrival }
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

TEST(CapacityAwareExposure, ConnectionExposureSplitsRideDurationAcrossOccupiedItems) {
    const auto exposure = connection_capacity_exposure(
          ride_connection(ride_leg(2, 4))
        , IntervalId{ 3 }
        , load_state({
              load(2, 50.0)
            , load(3, 100.0)
          })
        , capacity_set({
              capacity(2)
            , capacity(3)
          })
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    );

    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();
    EXPECT_DOUBLE_EQ(exposure->equivalent_time.value(), 15.0);
}

TEST(CapacityAwareExposure, DayPathSupportExposureUsesExcessPolicy) {
    const auto exposure = day_path_support_capacity_exposure(
          support_descriptor({
              support_leg(2, 4)
          })
        , IntervalId{ 3 }
        , load_state({
              load(2, 50.0)
            , load(3, 150.0)
          })
        , capacity_set({
              capacity(2)
            , capacity(3)
          })
        , CapacityPenaltyPolicy::ExcessVolumeCapacityRatio
    );

    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();
    EXPECT_DOUBLE_EQ(exposure->equivalent_time.value(), 5.0);
}

TEST(CapacityAwareExposure, MissingLoadContributesZeroExposure) {
    const auto exposure = connection_capacity_exposure(
          ride_connection(ride_leg(2, 4))
        , IntervalId{ 3 }
        , load_state({})
        , capacity_set({
              capacity(2)
            , capacity(3)
          })
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    );

    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();
    EXPECT_DOUBLE_EQ(exposure->equivalent_time.value(), 0.0);
}

TEST(CapacityAwareExposure, RejectsMissingCapacityAndInvalidSupportShape) {
    EXPECT_FALSE(connection_capacity_exposure(
          ride_connection(ride_leg(2, 4))
        , IntervalId{ 3 }
        , load_state({
              load(2, 50.0)
          })
        , capacity_set({
              capacity(2)
          })
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    ).has_value());

    EXPECT_FALSE(day_path_support_capacity_exposure(
          support_descriptor({
              support_leg(2, 2)
          })
        , IntervalId{ 3 }
        , load_state({})
        , capacity_set({})
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    ).has_value());
}

}  // namespace timetable::domain::assignment
