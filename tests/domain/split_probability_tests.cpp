#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/probability.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] double sum_probabilities(
        const std::vector<SplitProbabilityMass>& values
    ) {
        return std::accumulate(
              values.begin()
            , values.end()
            , 0.0
            , [](double acc, SplitProbabilityMass value) {
                  return acc + value.get();
              }
        );
    }

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

}  // namespace

TEST(SplitProbability, EqualLogWeightsProduceEqualProbabilities) {
    const std::vector<SplitLogWeight> log_weights{
          SplitLogWeight{ 0.0 }
        , SplitLogWeight{ 0.0 }
        , SplitLogWeight{ 0.0 }
    };

    const auto allocation = normalize_split_log_weights(log_weights, SplitDemandMass{ 90.0 });

    ASSERT_TRUE(allocation.has_value()) << allocation.error().to_string();
    ASSERT_EQ(allocation->probabilities.size(), 3u);
    EXPECT_NEAR(allocation->probabilities[0].get(), 1.0 / 3.0, 1e-15);
    EXPECT_NEAR(allocation->probabilities[1].get(), 1.0 / 3.0, 1e-15);
    EXPECT_NEAR(allocation->probabilities[2].get(), 1.0 / 3.0, 1e-15);
    EXPECT_NEAR(allocation->passengers[0].get(), 30.0, 1e-12);
    EXPECT_NEAR(allocation->passengers[1].get(), 30.0, 1e-12);
    EXPECT_NEAR(allocation->passengers[2].get(), 30.0, 1e-12);
}

TEST(SplitProbability, ConservesProbabilityAndPassengerMass) {
    const std::vector<SplitLogWeight> log_weights{
          SplitLogWeight{ -2.0 }
        , SplitLogWeight{ 0.0 }
        , SplitLogWeight{ 3.0 }
        , SplitLogWeight{ 1.0 }
    };
    constexpr double demand = 1234.5;

    const auto allocation = normalize_split_log_weights(
          log_weights
        , SplitDemandMass{ demand }
    );

    ASSERT_TRUE(allocation.has_value()) << allocation.error().to_string();
    EXPECT_NEAR(sum_probabilities(allocation->probabilities), 1.0, 1e-15);
    EXPECT_NEAR(sum_passengers(allocation->passengers), demand, 1e-10);
    EXPECT_EQ(allocation->residual_index, 2u);
}

TEST(SplitProbability, StabilizesLargeLogWeights) {
    const std::vector<SplitLogWeight> log_weights{
          SplitLogWeight{ 1000.0 }
        , SplitLogWeight{ 1001.0 }
        , SplitLogWeight{ 999.0 }
    };

    const auto allocation = normalize_split_log_weights(log_weights, SplitDemandMass{ 1.0 });

    ASSERT_TRUE(allocation.has_value()) << allocation.error().to_string();
    EXPECT_TRUE(std::isfinite(allocation->probabilities[0].get()));
    EXPECT_TRUE(std::isfinite(allocation->probabilities[1].get()));
    EXPECT_TRUE(std::isfinite(allocation->probabilities[2].get()));
    EXPECT_NEAR(sum_probabilities(allocation->probabilities), 1.0, 1e-15);
    EXPECT_EQ(allocation->residual_index, 1u);
}

TEST(SplitProbability, SuppressesNumericallyInsignificantSupportsConservatively) {
    const std::vector<SplitLogWeight> log_weights{
          SplitLogWeight{ 0.0 }
        , SplitLogWeight{ -40.0 }
    };
    constexpr double demand = 100.0;

    const auto allocation = normalize_split_log_weights(
          log_weights
        , SplitDemandMass{ demand }
    );

    ASSERT_TRUE(allocation.has_value()) << allocation.error().to_string();
    EXPECT_EQ(allocation->suppressed_numerical_shares, 1u);
    EXPECT_EQ(allocation->probabilities[1].get(), 0.0);
    EXPECT_EQ(allocation->passengers[1].get(), 0.0);
    EXPECT_NEAR(sum_probabilities(allocation->probabilities), 1.0, 1e-15);
    EXPECT_NEAR(sum_passengers(allocation->passengers), demand, 1e-12);
}

TEST(SplitProbability, ZeroDemandConservesProbabilityAndZeroPassengerMass) {
    const std::vector<SplitLogWeight> log_weights{
          SplitLogWeight{ 0.0 }
        , SplitLogWeight{ 1.0 }
    };

    const auto allocation = normalize_split_log_weights(log_weights, SplitDemandMass{ 0.0 });

    ASSERT_TRUE(allocation.has_value()) << allocation.error().to_string();
    EXPECT_NEAR(sum_probabilities(allocation->probabilities), 1.0, 1e-15);
    EXPECT_EQ(sum_passengers(allocation->passengers), 0.0);
}

TEST(SplitProbability, RejectsInvalidInputs) {
    const std::vector<SplitLogWeight> empty;
    const std::vector<SplitLogWeight> non_finite{
          SplitLogWeight{ 0.0 }
        , SplitLogWeight{ std::numeric_limits<double>::quiet_NaN() }
    };
    const std::vector<SplitLogWeight> valid{ SplitLogWeight{ 0.0 } };

    EXPECT_FALSE(normalize_split_log_weights(empty, SplitDemandMass{ 1.0 }).has_value());
    EXPECT_FALSE(normalize_split_log_weights(non_finite, SplitDemandMass{ 1.0 }).has_value());
    EXPECT_FALSE(normalize_split_log_weights(valid, SplitDemandMass{ -1.0 }).has_value());
    EXPECT_FALSE(normalize_split_log_weights(
          valid
        , SplitDemandMass{ std::numeric_limits<double>::infinity() }
    ).has_value());
}

}  // namespace timetable::domain::assignment
