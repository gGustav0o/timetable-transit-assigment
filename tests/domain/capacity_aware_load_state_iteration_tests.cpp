#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/capacity_aware/load_state_iteration.hpp"

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

    [[nodiscard]] VehicleJourneyItemLoadState state(
        std::vector<VehicleJourneyItemLoad> items
    ) {
        return VehicleJourneyItemLoadState{
            .items = std::move(items)
        };
    }

}  // namespace

TEST(CapacityAwareLoadStateIteration, DeltaUsesUnionOfSparseLoadKeys) {
    const auto delta = load_state_delta(
          state({
                load(2, 10.0)
              , load(3, 40.0)
          })
        , state({
                load(3, 10.0)
              , load(4, 20.0)
          })
    );

    EXPECT_DOUBLE_EQ(delta.max_absolute, 30.0);
    EXPECT_DOUBLE_EQ(delta.max_relative, 1.0);
}

TEST(CapacityAwareLoadStateIteration, MsaUpdateBuildsConvexSparseState) {
    const auto updated = msa_update_load_state(
          state({
                load(2, 10.0)
              , load(3, 40.0)
          })
        , VehicleJourneyItemLoads{
              .items = {
                    load(3, 10.0)
                  , load(4, 20.0)
              }
          }
        , 0.5
    );

    ASSERT_TRUE(updated.has_value()) << updated.error().to_string();
    ASSERT_EQ(updated->items.size(), 3u);
    EXPECT_EQ(updated->items[0].key, load_key(2));
    EXPECT_EQ(updated->items[1].key, load_key(3));
    EXPECT_EQ(updated->items[2].key, load_key(4));
    EXPECT_DOUBLE_EQ(updated->items[0].passengers, 5.0);
    EXPECT_DOUBLE_EQ(updated->items[1].passengers, 25.0);
    EXPECT_DOUBLE_EQ(updated->items[2].passengers, 10.0);
}

TEST(CapacityAwareLoadStateIteration, MsaUpdateDropsZeroRows) {
    const auto updated = msa_update_load_state(
          state({
              load(2, 10.0)
          })
        , VehicleJourneyItemLoads{
              .items = {}
          }
        , 1.0
    );

    ASSERT_TRUE(updated.has_value()) << updated.error().to_string();
    EXPECT_TRUE(updated->items.empty());
}

TEST(CapacityAwareLoadStateIteration, MsaUpdateRejectsInvalidAlphaAndLoads) {
    EXPECT_FALSE(msa_update_load_state(
          state({})
        , VehicleJourneyItemLoads{}
        , 0.0
    ).has_value());

    EXPECT_FALSE(msa_update_load_state(
          state({})
        , VehicleJourneyItemLoads{
              .items = {
                  load(2, std::numeric_limits<double>::quiet_NaN())
              }
          }
        , 1.0
    ).has_value());
}

TEST(CapacityAwareLoadStateIteration, ConvergenceUsesEitherTolerance) {
    const auto iteration = CapacityIterationConfig{
          .max_iterations = 10
        , .absolute_load_tolerance = 0.1
        , .relative_load_tolerance = 0.05
    };

    EXPECT_TRUE(capacity_iteration_converged(
          iteration
        , CapacityLoadStateDelta{
              .max_absolute = 0.05
            , .max_relative = 0.9
          }
    ));
    EXPECT_TRUE(capacity_iteration_converged(
          iteration
        , CapacityLoadStateDelta{
              .max_absolute = 10.0
            , .max_relative = 0.01
          }
    ));
    EXPECT_FALSE(capacity_iteration_converged(
          iteration
        , CapacityLoadStateDelta{
              .max_absolute = 0.2
            , .max_relative = 0.1
          }
    ));
}

}  // namespace timetable::domain::assignment
