#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/capacity_aware/load_state.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemLoadKey load_key(
        std::int64_t from_index
    ) {
        return VehicleJourneyItemLoadKey{
              .interval = IntervalId{ 3 }
            , .item = VehicleJourneyItemKey{
                  .trip = TripId{ 11 }
                , .from_index = RoutePosition{ from_index }
              }
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

}  // namespace

TEST(CapacityAwareLoadState, BuildsValidSparseLoadState) {
    const auto state = make_vehicle_journey_item_load_state(
        std::vector<VehicleJourneyItemLoad>{
              load(2, 10.0)
            , load(3, 20.0)
        }
    );

    ASSERT_TRUE(state.has_value()) << state.error().to_string();
    ASSERT_EQ(state->items.size(), 2u);
    EXPECT_EQ(state->items[0].key, load_key(2));
    EXPECT_DOUBLE_EQ(state->items[0].passengers, 10.0);
    EXPECT_EQ(state->items[1].key, load_key(3));
    EXPECT_DOUBLE_EQ(state->items[1].passengers, 20.0);
}

TEST(CapacityAwareLoadState, BuildsFromVehicleJourneyItemLoads) {
    const auto state = make_vehicle_journey_item_load_state(
        VehicleJourneyItemLoads{
            .items = {
                  load(2, 10.0)
                , load(3, 20.0)
            }
        }
    );

    ASSERT_TRUE(state.has_value()) << state.error().to_string();
    EXPECT_EQ(state->items.size(), 2u);
}

TEST(CapacityAwareLoadState, RejectsInvalidLoads) {
    EXPECT_FALSE(make_vehicle_journey_item_load_state(
        std::vector<VehicleJourneyItemLoad>{
            load(2, std::numeric_limits<double>::quiet_NaN())
        }
    ).has_value());

    EXPECT_FALSE(validate_vehicle_journey_item_load_state(
        VehicleJourneyItemLoadState{
            .items = {
                  load(2, 1.0)
                , load(2, 2.0)
            }
        }
    ).has_value());
}

}  // namespace timetable::domain::assignment
