#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/split_kernel.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] double sum_passengers(
        const std::vector<SplitPassengerMass>& values
    ) {
        return std::accumulate(
              values.begin()
            , values.end()
            , 0.0
            , [](double acc, SplitPassengerMass value) {
                  return acc + value.get();
              }
        );
    }

    [[nodiscard]] SplitKernelPolicy default_policy() {
        return SplitKernelPolicy{
              .probability = ProbabilityPolicy{}
            , .demand_projection = DemandProjectionPolicy{}
            , .load_accumulation = LoadAccumulationPolicy{}
        };
    }

}  // namespace

TEST(SplitKernel, ComposesProbabilityDemandProjectionAndLoadAccumulation) {
    const std::vector<SplitLogWeight> log_weights{
          SplitLogWeight{ 0.0 }
        , SplitLogWeight{ 0.0 }
    };
    const std::vector<std::size_t> trip_indices{ 0u, 0u };
    const std::vector<std::size_t> stop_indices{ 1u, 1u };
    const std::vector<std::size_t> segment_indices{ 0u, 1u };

    const auto result = execute_split_kernel(
          log_weights
        , SplitDemandMass{ 60.0 }
        , trip_indices
        , stop_indices
        , segment_indices
        , default_policy()
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->probabilities.size(), 2u);
    ASSERT_EQ(result->passengers.size(), 2u);
    EXPECT_NEAR(result->probabilities[0].get(), 0.5, 1e-15);
    EXPECT_NEAR(result->probabilities[1].get(), 0.5, 1e-15);
    EXPECT_DOUBLE_EQ(result->passengers[0].get(), 30.0);
    EXPECT_DOUBLE_EQ(result->passengers[1].get(), 30.0);
    EXPECT_DOUBLE_EQ(result->trip_loads[0], 60.0);
    EXPECT_DOUBLE_EQ(result->stop_loads[1], 60.0);
    EXPECT_DOUBLE_EQ(result->segment_loads[0], 30.0);
    EXPECT_DOUBLE_EQ(result->segment_loads[1], 30.0);
    EXPECT_EQ(result->total_alternatives, 2u);
    EXPECT_DOUBLE_EQ(result->total_demand, 60.0);
    EXPECT_DOUBLE_EQ(result->total_load, 60.0);
}

TEST(SplitKernel, UsesPolicyForNumericalSuppressionAndLoadSignificance) {
    const std::vector<SplitLogWeight> log_weights{
          SplitLogWeight{ 0.0 }
        , SplitLogWeight{ -40.0 }
    };
    const std::vector<std::size_t> indices{ 0u, 1u };
    auto policy = default_policy();
    policy.probability.numerical_suppression_threshold = 1e-8;
    policy.load_accumulation.load_accumulation_threshold = 1.0;

    const auto result = execute_split_kernel(
          log_weights
        , SplitDemandMass{ 100.0 }
        , indices
        , indices
        , indices
        , policy
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_EQ(result->suppressed_alternatives, 1u);
    EXPECT_EQ(result->segment_loads[1], 0.0);
    EXPECT_EQ(result->significant_alternatives, 1u);
    EXPECT_DOUBLE_EQ(sum_passengers(result->passengers), 100.0);
}

TEST(SplitKernel, PropagatesInvalidInputsAsExpected) {
    const std::vector<SplitLogWeight> empty;
    const std::vector<SplitLogWeight> valid{ SplitLogWeight{ 0.0 } };
    const std::vector<SplitLogWeight> non_finite{
          SplitLogWeight{ 0.0 }
        , SplitLogWeight{ std::numeric_limits<double>::quiet_NaN() }
    };
    const std::vector<std::size_t> one{ 0u };
    const std::vector<std::size_t> two{ 0u, 1u };

    EXPECT_FALSE(execute_split_kernel(
          empty
        , SplitDemandMass{ 1.0 }
        , one
        , one
        , one
        , default_policy()
    ).has_value());
    EXPECT_FALSE(execute_split_kernel(
          valid
        , SplitDemandMass{ -1.0 }
        , one
        , one
        , one
        , default_policy()
    ).has_value());
    EXPECT_FALSE(execute_split_kernel(
          valid
        , SplitDemandMass{ 1.0 }
        , two
        , one
        , one
        , default_policy()
    ).has_value());
    EXPECT_FALSE(execute_split_kernel(
          non_finite
        , SplitDemandMass{ 1.0 }
        , two
        , two
        , two
        , default_policy()
    ).has_value());
}

}  // namespace timetable::domain::assignment
