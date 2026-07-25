#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/cost/capacity_index.hpp"

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

}  // namespace

TEST(SearchCostCapacityIndex, BuildsSortedPenaltyPrefixesPerObservedIntervalAndTrip) {
    const auto loads = load_state({
          load(IntervalId{ 3 }, TripId{ 11 }, 3, 100.0)
        , load(IntervalId{ 3 }, TripId{ 11 }, 2, 50.0)
        , load(IntervalId{ 4 }, TripId{ 11 }, 2, 150.0)
    });
    const auto capacities = capacity_set({
          capacity(TripId{ 11 }, 3)
        , capacity(TripId{ 11 }, 2)
    });

    const auto index = build_search_capacity_cost_index(
          loads
        , capacities
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    );

    ASSERT_TRUE(index.has_value()) << index.error().message();
    const auto prefix = index->penalty_prefixes.find(
        SearchCapacityTripPenaltyKey{ .interval = IntervalId{ 3 }, .trip = TripId{ 11 } }
    );
    ASSERT_NE(prefix, index->penalty_prefixes.end());
    ASSERT_EQ(prefix->second.positions.size(), 2u);
    EXPECT_EQ(prefix->second.positions[0], RoutePosition{ 2 });
    EXPECT_EQ(prefix->second.positions[1], RoutePosition{ 3 });
    ASSERT_EQ(prefix->second.cumulative_penalties.size(), 3u);
    EXPECT_DOUBLE_EQ(prefix->second.cumulative_penalties[0], 0.0);
    EXPECT_DOUBLE_EQ(prefix->second.cumulative_penalties[1], 0.5);
    EXPECT_DOUBLE_EQ(prefix->second.cumulative_penalties[2], 1.5);
}

TEST(SearchCostCapacityIndex, ComputesPenaltySumOverHalfOpenOccupiedRange) {
    const auto loads = load_state({
          load(IntervalId{ 3 }, TripId{ 11 }, 2, 50.0)
        , load(IntervalId{ 3 }, TripId{ 11 }, 3, 100.0)
        , load(IntervalId{ 3 }, TripId{ 11 }, 4, 25.0)
    });
    const auto capacities = capacity_set({
          capacity(TripId{ 11 }, 2)
        , capacity(TripId{ 11 }, 3)
        , capacity(TripId{ 11 }, 4)
    });
    auto config = SearchCapacityCostConfig{
          .load_state   = loads
        , .capacity_set = capacities
    };
    auto index = build_search_capacity_cost_index(
          config.load_state
        , config.capacity_set
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    );
    ASSERT_TRUE(index.has_value()) << index.error().message();
    config.index = *std::move(index);

    const auto sum = search_capacity_penalty_sum(
          config
        , IntervalId{ 3 }
        , TripId{ 11 }
        , RoutePosition{ 2 }
        , RoutePosition{ 4 }
    );

    ASSERT_TRUE(sum.has_value()) << sum.error().message();
    EXPECT_DOUBLE_EQ(*sum, 1.5);
}

TEST(SearchCostCapacityIndex, RejectsMissingCapacityInOccupiedRange) {
    auto config = SearchCapacityCostConfig{
          .capacity_set = capacity_set({
              capacity(TripId{ 11 }, 2)
          })
    };
    auto index = build_search_capacity_cost_index(
          config.load_state
        , config.capacity_set
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    );
    ASSERT_TRUE(index.has_value()) << index.error().message();
    config.index = *std::move(index);

    EXPECT_FALSE(search_capacity_penalty_sum(
          config
        , IntervalId{ 3 }
        , TripId{ 11 }
        , RoutePosition{ 2 }
        , RoutePosition{ 4 }
    ).has_value());
}

TEST(SearchCostCapacityIndex, RejectsInconsistentPrefixIndex) {
    auto config = SearchCapacityCostConfig{
          .load_state = load_state({
              load(IntervalId{ 3 }, TripId{ 11 }, 2, 50.0)
          })
        , .capacity_set = capacity_set({
              capacity(TripId{ 11 }, 2)
          })
    };
    auto index = build_search_capacity_cost_index(
          config.load_state
        , config.capacity_set
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    );
    ASSERT_TRUE(index.has_value()) << index.error().message();
    config.index = *std::move(index);
    config.index.penalty_prefixes.begin()->second.cumulative_penalties = { 0.0, -1.0 };

    EXPECT_FALSE(validate_search_capacity_cost_index(config).has_value());
}

}  // namespace timetable::domain::assignment
