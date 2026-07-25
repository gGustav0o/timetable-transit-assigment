#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/capacity/overload.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemKey item_key(
        std::int64_t from_index = 2
    ) {
        return VehicleJourneyItemKey{
              .trip = TripId{ 7 }
            , .from_index = RoutePosition{ from_index }
        };
    }

    [[nodiscard]] VehicleJourneyItemLoadKey load_key(
        VehicleJourneyItemKey item = item_key()
    ) {
        return VehicleJourneyItemLoadKey{
              .interval = IntervalId{ 3 }
            , .item = item
        };
    }

    [[nodiscard]] VehicleJourneyItemLoad load(
        double passengers
    ) {
        return VehicleJourneyItemLoad{
              .key = load_key()
            , .passengers = passengers
        };
    }

    [[nodiscard]] VehicleJourneyItemCapacity capacity(
          double total_capacity
        , double seat_capacity = 10.0
        , VehicleJourneyItemKey key = item_key()
    ) {
        return VehicleJourneyItemCapacity{
              .key = key
            , .total_capacity = total_capacity
            , .seat_capacity = seat_capacity
        };
    }

}  // namespace

TEST(CapacityOverload, StatusClassifiesCapacityCases) {
    const auto ok = vehicle_journey_item_overload_status(9.0, 10.0);
    const auto full = vehicle_journey_item_overload_status(10.0, 10.0);
    const auto overloaded = vehicle_journey_item_overload_status(11.0, 10.0);
    const auto zero_capacity = vehicle_journey_item_overload_status(1.0, 0.0);

    ASSERT_TRUE(ok.has_value()) << ok.error().to_string();
    ASSERT_TRUE(full.has_value()) << full.error().to_string();
    ASSERT_TRUE(overloaded.has_value()) << overloaded.error().to_string();
    ASSERT_TRUE(zero_capacity.has_value()) << zero_capacity.error().to_string();
    EXPECT_EQ(*ok, VehicleJourneyItemOverloadStatus::Ok);
    EXPECT_EQ(*full, VehicleJourneyItemOverloadStatus::Ok);
    EXPECT_EQ(*overloaded, VehicleJourneyItemOverloadStatus::Overloaded);
    EXPECT_EQ(*zero_capacity, VehicleJourneyItemOverloadStatus::ZeroCapacity);
}

TEST(CapacityOverload, ComputesOkAndOverloadedRows) {
    const auto ok = compute_vehicle_journey_item_overload(load(8.0), capacity(10.0));
    const auto overloaded = compute_vehicle_journey_item_overload(load(13.0), capacity(10.0));

    ASSERT_TRUE(ok.has_value()) << ok.error().to_string();
    ASSERT_TRUE(overloaded.has_value()) << overloaded.error().to_string();
    EXPECT_EQ(ok->status, VehicleJourneyItemOverloadStatus::Ok);
    ASSERT_TRUE(ok->load_factor.has_value());
    ASSERT_TRUE(ok->overload_passengers.has_value());
    EXPECT_DOUBLE_EQ(*ok->load_factor, 0.8);
    EXPECT_DOUBLE_EQ(*ok->overload_passengers, 0.0);

    EXPECT_EQ(overloaded->status, VehicleJourneyItemOverloadStatus::Overloaded);
    ASSERT_TRUE(overloaded->load_factor.has_value());
    ASSERT_TRUE(overloaded->overload_passengers.has_value());
    EXPECT_DOUBLE_EQ(*overloaded->load_factor, 1.3);
    EXPECT_DOUBLE_EQ(*overloaded->overload_passengers, 3.0);
}

TEST(CapacityOverload, ComputesZeroCapacityRowWithoutLoadFactor) {
    const auto result = compute_vehicle_journey_item_overload(
          load(4.0)
        , capacity(0.0, 0.0)
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_EQ(result->status, VehicleJourneyItemOverloadStatus::ZeroCapacity);
    EXPECT_FALSE(result->load_factor.has_value());
    ASSERT_TRUE(result->overload_passengers.has_value());
    EXPECT_DOUBLE_EQ(*result->overload_passengers, 4.0);
}

TEST(CapacityOverload, MissingCapacityRowHasNoCapacityDerivedValues) {
    const auto result = make_missing_capacity_vehicle_journey_item_overload(load(5.0));

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_EQ(result->status, VehicleJourneyItemOverloadStatus::MissingCapacity);
    EXPECT_FALSE(result->total_capacity.has_value());
    EXPECT_FALSE(result->seat_capacity.has_value());
    EXPECT_FALSE(result->load_factor.has_value());
    EXPECT_FALSE(result->overload_passengers.has_value());
}

TEST(CapacityOverload, RejectsInvalidInputs) {
    EXPECT_FALSE(vehicle_journey_item_overload_status(-1.0, 10.0).has_value());
    EXPECT_FALSE(vehicle_journey_item_overload_status(
          1.0
        , std::numeric_limits<double>::infinity()
    ).has_value());
    EXPECT_FALSE(compute_vehicle_journey_item_overload(
          load(1.0)
        , capacity(1.0, 1.0, item_key(4))
    ).has_value());
    EXPECT_FALSE(make_missing_capacity_vehicle_journey_item_overload(
        load(std::numeric_limits<double>::quiet_NaN())
    ).has_value());
}

}  // namespace timetable::domain::assignment
