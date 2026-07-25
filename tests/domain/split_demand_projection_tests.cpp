#include <limits>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/demand_projection.hpp"

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

}  // namespace

TEST(SplitDemandProjection, ProjectsProbabilitiesToPassengerMass) {
    const std::vector<SplitProbabilityMass> probabilities{
          SplitProbabilityMass{ 0.2 }
        , SplitProbabilityMass{ 0.3 }
        , SplitProbabilityMass{ 0.5 }
    };

    const auto projection = project_demand(
          probabilities
        , SplitDemandMass{ 100.0 }
        , 2u
    );

    ASSERT_TRUE(projection.has_value()) << projection.error().to_string();
    EXPECT_DOUBLE_EQ(projection->passengers[0].get(), 20.0);
    EXPECT_DOUBLE_EQ(projection->passengers[1].get(), 30.0);
    EXPECT_DOUBLE_EQ(projection->passengers[2].get(), 50.0);
    EXPECT_DOUBLE_EQ(sum_passengers(projection->passengers), 100.0);
    EXPECT_EQ(projection->residual_index, 2u);
}

TEST(SplitDemandProjection, ResidualAlternativeReceivesRoundingMass) {
    const std::vector<SplitProbabilityMass> probabilities{
          SplitProbabilityMass{ 0.1 }
        , SplitProbabilityMass{ 0.2 }
        , SplitProbabilityMass{ 0.7 }
    };

    const auto projection = project_demand(
          probabilities
        , SplitDemandMass{ 0.3 }
        , 2u
    );

    ASSERT_TRUE(projection.has_value()) << projection.error().to_string();
    EXPECT_NEAR(sum_passengers(projection->passengers), 0.3, 1e-15);
    EXPECT_DOUBLE_EQ(
          projection->passengers[2].get()
        , 0.3 - projection->passengers[0].get() - projection->passengers[1].get()
    );
}

TEST(SplitDemandProjection, ZeroDemandProducesZeroPassengerMass) {
    const std::vector<SplitProbabilityMass> probabilities{
          SplitProbabilityMass{ 0.25 }
        , SplitProbabilityMass{ 0.75 }
    };

    const auto projection = project_demand(
          probabilities
        , SplitDemandMass{ 0.0 }
        , 1u
    );

    ASSERT_TRUE(projection.has_value()) << projection.error().to_string();
    EXPECT_DOUBLE_EQ(sum_passengers(projection->passengers), 0.0);
}

TEST(SplitDemandProjection, CountsSuppressedAlternativesByPolicy) {
    const std::vector<SplitProbabilityMass> probabilities{
          SplitProbabilityMass{ 0.999 }
        , SplitProbabilityMass{ 0.001 }
    };

    const auto projection = project_demand(
          probabilities
        , SplitDemandMass{ 100.0 }
        , 0u
        , DemandProjectionPolicy{
              .significant_probability_threshold = 0.01
            , .passenger_count_tolerance = 0.02
          }
    );

    ASSERT_TRUE(projection.has_value()) << projection.error().to_string();
    EXPECT_EQ(projection->significant_alternatives, 1u);
    EXPECT_EQ(projection->suppressed_alternatives, 1u);
}

TEST(SplitDemandProjection, RejectsInvalidInputs) {
    const std::vector<SplitProbabilityMass> empty;
    const std::vector<SplitProbabilityMass> valid{ SplitProbabilityMass{ 1.0 } };
    const std::vector<SplitProbabilityMass> non_finite{
          SplitProbabilityMass{ 1.0 }
        , SplitProbabilityMass{ std::numeric_limits<double>::quiet_NaN() }
    };
    const std::vector<SplitProbabilityMass> negative{
          SplitProbabilityMass{ 1.1 }
        , SplitProbabilityMass{ -0.1 }
    };
    const std::vector<SplitProbabilityMass> not_normalized{
          SplitProbabilityMass{ 0.25 }
        , SplitProbabilityMass{ 0.25 }
    };

    EXPECT_FALSE(project_demand(empty, SplitDemandMass{ 1.0 }, 0u).has_value());
    EXPECT_FALSE(project_demand(valid, SplitDemandMass{ 1.0 }, 1u).has_value());
    EXPECT_FALSE(project_demand(valid, SplitDemandMass{ -1.0 }, 0u).has_value());
    EXPECT_FALSE(project_demand(non_finite, SplitDemandMass{ 1.0 }, 0u).has_value());
    EXPECT_FALSE(project_demand(negative, SplitDemandMass{ 1.0 }, 0u).has_value());
    EXPECT_FALSE(project_demand(not_normalized, SplitDemandMass{ 1.0 }, 0u).has_value());
    EXPECT_FALSE(project_demand(
          valid
        , SplitDemandMass{ 1.0 }
        , 0u
        , DemandProjectionPolicy{
              .significant_probability_threshold =
                  std::numeric_limits<double>::quiet_NaN()
            , .passenger_count_tolerance = 0.0
          }
    ).has_value());
}

}  // namespace timetable::domain::assignment
