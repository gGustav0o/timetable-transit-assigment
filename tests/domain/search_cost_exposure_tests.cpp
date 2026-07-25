#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search/cost/capacity_index.hpp"
#include "timetable/domain/assignment/search/cost/exposure.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemKey item_key(
          TripId       trip
        , std::int64_t from_index
    ) {
        return VehicleJourneyItemKey{
              .trip       = trip
            , .from_index = RoutePosition{ from_index }
        };
    }

    [[nodiscard]] VehicleJourneyItemCapacity capacity(
          TripId       trip
        , std::int64_t from_index
        , double       total_capacity = 100.0
    ) {
        return VehicleJourneyItemCapacity{
              .key            = item_key(trip, from_index)
            , .total_capacity = total_capacity
            , .seat_capacity  = total_capacity
        };
    }

    [[nodiscard]] VehicleJourneyItemLoad load(
          IntervalId   interval
        , TripId       trip
        , std::int64_t from_index
        , double       passengers
    ) {
        return VehicleJourneyItemLoad{
              .key = VehicleJourneyItemLoadKey{
                    .interval = interval
                  , .item     = item_key(trip, from_index)
              }
            , .passengers = passengers
        };
    }

    [[nodiscard]] VehicleJourneyItemLoadState load_state(
        std::vector<VehicleJourneyItemLoad> items
    ) {
        return VehicleJourneyItemLoadState{ .items = std::move(items) };
    }

    [[nodiscard]] VehicleJourneyItemCapacitySet capacity_set(
        std::vector<VehicleJourneyItemCapacity> items
    ) {
        return VehicleJourneyItemCapacitySet{ .items = std::move(items) };
    }

    [[nodiscard]] mathfp::Expected<SearchCapacityCostConfig> search_capacity_config(
          VehicleJourneyItemLoadState   loads
        , VehicleJourneyItemCapacitySet capacities
        , CapacityPenaltyPolicy         penalty_policy = CapacityPenaltyPolicy::VolumeCapacityRatio
    ) {
        SearchCapacityCostConfig config{
              .volume_capacity_ratio = Dimless{ 0.25 }
            , .penalty_policy        = penalty_policy
            , .load_state            = std::move(loads)
            , .capacity_set          = std::move(capacities)
        };

        MATHFP_TRY_LET(
              SearchCapacityCostIndex
            , index
            , build_search_capacity_cost_index(
                  config.load_state
                , config.capacity_set
                , config.penalty_policy
            )
        );
        config.index = std::move(index);
        return config;
    }

    [[nodiscard]] ConnectionLeg ride_leg(
          TripId       trip
        , std::int64_t from_index
        , std::int64_t to_index
        , double       start_time = 10.0
        , double       end_time = 30.0
    ) {
        return ConnectionLeg{
              .kind               = ConnectionLegKind::Ride
            , .connection_segment = ConnectionSegmentId{ 40 }
            , .route_segment      = RouteSegmentId{ 50 }
            , .physical_from      = endpoint_key(StopId{ 100 + from_index })
            , .physical_to        = endpoint_key(StopId{ 100 + to_index })
            , .occurrence_from    = StopOccurrenceKey{
                  .stop     = StopId{ 100 + from_index }
                , .position = RoutePosition{ from_index }
              }
            , .occurrence_to      = StopOccurrenceKey{
                  .stop     = StopId{ 100 + to_index }
                , .position = RoutePosition{ to_index }
              }
            , .line               = LineId{ 7 }
            , .route              = RouteId{ 8 }
            , .trip               = trip
            , .start_time         = Time{ start_time }
            , .end_time           = Time{ end_time }
            , .length             = Length{ 2.0 }
            , .fare               = 0.0
        };
    }

    [[nodiscard]] ConnectionLeg walk_leg() {
        return ConnectionLeg{
              .kind          = ConnectionLegKind::AccessWalk
            , .physical_from = endpoint_key(ZoneId{ 1 })
            , .physical_to   = endpoint_key(StopId{ 102 })
            , .start_time    = Time{ 0.0 }
            , .end_time      = Time{ 10.0 }
            , .length        = Length{ 1.0 }
            , .fare          = 0.0
        };
    }

    [[nodiscard]] Connection connection(
        std::vector<ConnectionLeg> legs
    ) {
        return Connection{
              .origin      = ZoneId{ 1 }
            , .destination = ZoneId{ 2 }
            , .trace       = ConnectionTrace{ .legs = std::move(legs) }
        };
    }

}  // namespace

TEST(SearchCostExposure, RideLegExposureUsesPrefixPenaltyAverageDuration) {
    const auto config = search_capacity_config(
          load_state({
              load(IntervalId{ 3 }, TripId{ 11 }, 2, 50.0)
            , load(IntervalId{ 3 }, TripId{ 11 }, 3, 100.0)
          })
        , capacity_set({
              capacity(TripId{ 11 }, 2)
            , capacity(TripId{ 11 }, 3)
          })
    );
    ASSERT_TRUE(config.has_value()) << config.error().to_string();

    const auto exposure = search_capacity_exposure(
          ride_leg(TripId{ 11 }, 2, 4)
        , IntervalId{ 3 }
        , *config
    );

    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();
    EXPECT_DOUBLE_EQ(exposure->equivalent_time.value(), 15.0);
}

TEST(SearchCostExposure, ConnectionExposureSumsRideLegsAndIgnoresWalkLegs) {
    const auto config = search_capacity_config(
          load_state({
              load(IntervalId{ 3 }, TripId{ 11 }, 2, 50.0)
            , load(IntervalId{ 3 }, TripId{ 11 }, 3, 100.0)
            , load(IntervalId{ 3 }, TripId{ 12 }, 4, 150.0)
          })
        , capacity_set({
              capacity(TripId{ 11 }, 2)
            , capacity(TripId{ 11 }, 3)
            , capacity(TripId{ 12 }, 4)
          })
    );
    ASSERT_TRUE(config.has_value()) << config.error().to_string();

    const auto exposure = search_capacity_exposure(
          connection({
              walk_leg()
            , ride_leg(TripId{ 11 }, 2, 4, 10.0, 30.0)
            , ride_leg(TripId{ 12 }, 4, 5, 35.0, 45.0)
          })
        , IntervalId{ 3 }
        , *config
    );

    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();
    EXPECT_DOUBLE_EQ(exposure->equivalent_time.value(), 30.0);
}

TEST(SearchCostExposure, MissingLoadContributesZeroExposure) {
    const auto config = search_capacity_config(
          load_state({})
        , capacity_set({
              capacity(TripId{ 11 }, 2)
            , capacity(TripId{ 11 }, 3)
          })
    );
    ASSERT_TRUE(config.has_value()) << config.error().to_string();

    const auto exposure = search_capacity_exposure(
          ride_leg(TripId{ 11 }, 2, 4)
        , IntervalId{ 3 }
        , *config
    );

    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();
    EXPECT_DOUBLE_EQ(exposure->equivalent_time.value(), 0.0);
}

TEST(SearchCostExposure, RejectsMissingCapacityInOccupiedRange) {
    const auto config = search_capacity_config(
          load_state({
              load(IntervalId{ 3 }, TripId{ 11 }, 2, 50.0)
          })
        , capacity_set({
              capacity(TripId{ 11 }, 2)
          })
    );
    ASSERT_TRUE(config.has_value()) << config.error().to_string();

    EXPECT_FALSE(search_capacity_exposure(
          ride_leg(TripId{ 11 }, 2, 4)
        , IntervalId{ 3 }
        , *config
    ).has_value());
}

TEST(SearchCostExposure, RejectsNonRideLegAndInvalidRideIdentity) {
    const auto config = search_capacity_config(load_state({}), capacity_set({}));
    ASSERT_TRUE(config.has_value()) << config.error().to_string();

    EXPECT_FALSE(search_capacity_exposure(
          walk_leg()
        , IntervalId{ 3 }
        , *config
    ).has_value());

    auto missing_trip = ride_leg(TripId{ 11 }, 2, 4);
    missing_trip.trip = std::nullopt;
    EXPECT_FALSE(search_capacity_exposure(
          missing_trip
        , IntervalId{ 3 }
        , *config
    ).has_value());

    EXPECT_FALSE(search_capacity_exposure(
          ride_leg(TripId{ 11 }, 2, 2)
        , IntervalId{ 3 }
        , *config
    ).has_value());
}

}  // namespace timetable::domain::assignment
