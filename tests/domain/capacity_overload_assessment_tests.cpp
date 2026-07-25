#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/capacity/overload_assessment.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemKey item_key(
          std::int64_t trip
        , std::int64_t from_index
    ) {
        return VehicleJourneyItemKey{
              .trip = TripId{ trip }
            , .from_index = RoutePosition{ from_index }
        };
    }

    [[nodiscard]] VehicleJourneyItemLoad load(
          std::int64_t interval
        , VehicleJourneyItemKey key
        , double passengers
    ) {
        return VehicleJourneyItemLoad{
              .key = VehicleJourneyItemLoadKey{
                    .interval = IntervalId{ interval }
                  , .item = key
              }
            , .passengers = passengers
        };
    }

    [[nodiscard]] VehicleJourneyItemCapacity capacity(
          VehicleJourneyItemKey key
        , double total_capacity
    ) {
        return VehicleJourneyItemCapacity{
              .key = key
            , .total_capacity = total_capacity
            , .seat_capacity = total_capacity
        };
    }

    [[nodiscard]] TimeInterval interval(
          std::int64_t id
        , double start
        , double end
    ) {
        return TimeInterval{
              .id = IntervalId{ id }
            , .start = Time{ start }
            , .end = Time{ end }
        };
    }

}  // namespace

TEST(CapacityOverloadAssessment, MaterializesCapacityRowsForEveryInterval) {
    const auto item = item_key(7, 2);
    const VehicleJourneyItemLoads loads{
        .items = {
            load(1, item, 8.0)
        }
    };
    const VehicleJourneyItemCapacitySet capacities{
        .items = {
            capacity(item, 10.0)
        }
    };
    const std::vector<TimeInterval> intervals{
          interval(1, 0.0, 10.0)
        , interval(2, 10.0, 20.0)
    };

    const auto result = assess_vehicle_journey_item_overload(
          loads
        , capacities
        , intervals
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->status, VehicleJourneyItemOverloadAssessmentStatus::Calculated);
    ASSERT_EQ(result->items.size(), 2u);
    EXPECT_EQ(result->items[0].key.interval, IntervalId{ 1 });
    EXPECT_EQ(result->items[0].key.item, item);
    EXPECT_DOUBLE_EQ(result->items[0].passengers, 8.0);
    EXPECT_EQ(result->items[0].status, VehicleJourneyItemOverloadStatus::Ok);
    EXPECT_EQ(result->items[1].key.interval, IntervalId{ 2 });
    EXPECT_EQ(result->items[1].key.item, item);
    EXPECT_DOUBLE_EQ(result->items[1].passengers, 0.0);
    EXPECT_EQ(result->items[1].status, VehicleJourneyItemOverloadStatus::Ok);
}

TEST(CapacityOverloadAssessment, KeepsLoadedItemsWithoutCapacityAsMissingCapacityRows) {
    const auto capacity_item = item_key(7, 2);
    const auto missing_item = item_key(8, 1);
    const VehicleJourneyItemLoads loads{
        .items = {
              load(1, capacity_item, 8.0)
            , load(1, missing_item, 4.0)
        }
    };
    const VehicleJourneyItemCapacitySet capacities{
        .items = {
            capacity(capacity_item, 10.0)
        }
    };
    const std::vector<TimeInterval> intervals{
        interval(1, 0.0, 10.0)
    };

    const auto result = assess_vehicle_journey_item_overload(
          loads
        , capacities
        , intervals
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->items.size(), 2u);
    EXPECT_EQ(result->items[0].key.item, capacity_item);
    EXPECT_EQ(result->items[0].status, VehicleJourneyItemOverloadStatus::Ok);
    EXPECT_EQ(result->items[1].key.item, missing_item);
    EXPECT_EQ(result->items[1].status, VehicleJourneyItemOverloadStatus::MissingCapacity);
    EXPECT_FALSE(result->items[1].total_capacity.has_value());
}

TEST(CapacityOverloadAssessment, RejectsInvalidAssessmentSupport) {
    const auto item = item_key(7, 2);
    const VehicleJourneyItemCapacitySet capacities{
        .items = {
            capacity(item, 10.0)
        }
    };

    EXPECT_FALSE(assess_vehicle_journey_item_overload(
          VehicleJourneyItemLoads{
              .items = {
                  load(2, item, 1.0)
              }
          }
        , capacities
        , std::vector<TimeInterval>{
              interval(1, 0.0, 10.0)
          }
    ).has_value());

    EXPECT_FALSE(assess_vehicle_journey_item_overload(
          VehicleJourneyItemLoads{}
        , capacities
        , std::vector<TimeInterval>{
              interval(1, 0.0, 10.0)
            , interval(1, 10.0, 20.0)
          }
    ).has_value());
}

TEST(CapacityOverloadAssessment, NonCalculatedFactoriesAreEmpty) {
    EXPECT_EQ(
          make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment().status
        , VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled
    );
    EXPECT_TRUE(
        make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment().items.empty()
    );
    EXPECT_EQ(
          make_disabled_by_config_vehicle_journey_item_overload_assessment().status
        , VehicleJourneyItemOverloadAssessmentStatus::DisabledByConfig
    );
    EXPECT_TRUE(
        make_disabled_by_config_vehicle_journey_item_overload_assessment().items.empty()
    );
    EXPECT_EQ(
          make_missing_capacity_input_vehicle_journey_item_overload_assessment().status
        , VehicleJourneyItemOverloadAssessmentStatus::MissingCapacityInput
    );
    EXPECT_TRUE(
        make_missing_capacity_input_vehicle_journey_item_overload_assessment().items.empty()
    );
}

}  // namespace timetable::domain::assignment
