#include <limits>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/load_accumulation.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] double sum_loads(const std::vector<double>& loads) {
        return std::accumulate(loads.begin(), loads.end(), 0.0);
    }

}  // namespace

TEST(SplitLoadAccumulation, AccumulatesRepeatedTripStopAndSegmentIndices) {
    const std::vector<SplitPassengerMass> passengers{
          SplitPassengerMass{ 10.0 }
        , SplitPassengerMass{ 2.5 }
        , SplitPassengerMass{ 7.5 }
    };
    const std::vector<std::size_t> trip_indices{ 1u, 1u, 0u };
    const std::vector<std::size_t> stop_indices{ 2u, 2u, 3u };
    const std::vector<std::size_t> segment_indices{ 4u, 5u, 4u };

    const auto result = accumulate_loads(
          passengers
        , trip_indices
        , stop_indices
        , segment_indices
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->trip_loads.size(), 2u);
    ASSERT_EQ(result->stop_loads.size(), 4u);
    ASSERT_EQ(result->segment_loads.size(), 6u);
    EXPECT_DOUBLE_EQ(result->trip_loads[1], 12.5);
    EXPECT_DOUBLE_EQ(result->stop_loads[2], 12.5);
    EXPECT_DOUBLE_EQ(result->segment_loads[4], 17.5);
    EXPECT_DOUBLE_EQ(result->segment_loads[5], 2.5);
    EXPECT_DOUBLE_EQ(result->total_load, 20.0);
}

TEST(SplitLoadAccumulation, ZeroPassengerMassKeepsConservation) {
    const std::vector<SplitPassengerMass> passengers{
          SplitPassengerMass{ 0.0 }
        , SplitPassengerMass{ 5.0 }
    };
    const std::vector<std::size_t> indices{ 0u, 0u };

    const auto result = accumulate_loads(passengers, indices, indices, indices);

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_DOUBLE_EQ(sum_loads(result->trip_loads), 5.0);
    EXPECT_DOUBLE_EQ(sum_loads(result->stop_loads), 5.0);
    EXPECT_DOUBLE_EQ(sum_loads(result->segment_loads), 5.0);
    EXPECT_DOUBLE_EQ(result->total_load, 5.0);
}

TEST(SplitLoadAccumulation, CountsSignificantLoadsAcrossAllLoadVectors) {
    const std::vector<SplitPassengerMass> passengers{
          SplitPassengerMass{ 0.25 }
        , SplitPassengerMass{ 2.0 }
    };
    const std::vector<std::size_t> trip_indices{ 0u, 1u };
    const std::vector<std::size_t> stop_indices{ 0u, 0u };
    const std::vector<std::size_t> segment_indices{ 0u, 1u };

    const auto result = accumulate_loads(
          passengers
        , trip_indices
        , stop_indices
        , segment_indices
        , LoadAccumulationPolicy{ .load_accumulation_threshold = 1.0 }
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_EQ(result->significant_loads, 3u);
}

TEST(SplitLoadAccumulation, RejectsMismatchedIndexSpans) {
    const std::vector<SplitPassengerMass> passengers{
        SplitPassengerMass{ 1.0 }
    };
    const std::vector<std::size_t> one{ 0u };
    const std::vector<std::size_t> empty;

    EXPECT_FALSE(accumulate_loads(passengers, one, empty, one).has_value());
}

TEST(SplitLoadAccumulation, RejectsInvalidPassengerMassAndPolicy) {
    const std::vector<std::size_t> indices{ 0u };
    const std::vector<SplitPassengerMass> negative{
        SplitPassengerMass{ -1.0 }
    };
    const std::vector<SplitPassengerMass> non_finite{
        SplitPassengerMass{ std::numeric_limits<double>::quiet_NaN() }
    };
    const std::vector<SplitPassengerMass> valid{
        SplitPassengerMass{ 1.0 }
    };

    EXPECT_FALSE(accumulate_loads(negative, indices, indices, indices).has_value());
    EXPECT_FALSE(accumulate_loads(non_finite, indices, indices, indices).has_value());
    EXPECT_FALSE(accumulate_loads(
          valid
        , indices
        , indices
        , indices
        , LoadAccumulationPolicy{
              .load_accumulation_threshold =
                  std::numeric_limits<double>::infinity()
          }
    ).has_value());
}

TEST(SplitLoadAccumulation, RejectsTooLargeIndex) {
    const std::vector<SplitPassengerMass> passengers{
        SplitPassengerMass{ 1.0 }
    };
    const std::vector<std::size_t> huge{
        std::numeric_limits<std::size_t>::max()
    };

    EXPECT_FALSE(accumulate_loads(passengers, huge, huge, huge).has_value());
}

}  // namespace timetable::domain::assignment
