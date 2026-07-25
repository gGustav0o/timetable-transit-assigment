#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/independence.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] SplitIndependenceConfig independence_config(
        bool enabled = true
    ) {
        return SplitIndependenceConfig{
              .enabled = enabled
            , .gamma = Dimless{ 0.5 }
            , .temporal_similarity_scale = Dimless{ 30.0 }
            , .higher_quality_scale = Dimless{ 1.0 }
            , .lower_quality_scale = Dimless{ 1.0 }
            , .higher_perceived_journey_time_scale = Dimless{ 30.0 }
            , .lower_perceived_journey_time_scale = Dimless{ 30.0 }
            , .higher_fare_scale = Dimless{ 5.0 }
            , .lower_fare_scale = Dimless{ 5.0 }
        };
    }

    [[nodiscard]] SplitIndependenceAlternativeView alternative(
          double departure
        , double arrival
        , double perceived_journey_time
        , double fare
    ) {
        return SplitIndependenceAlternativeView{
              .departure_time = departure
            , .arrival_time = arrival
            , .perceived_journey_time = perceived_journey_time
            , .fare = fare
        };
    }

}  // namespace

TEST(SplitIndependence, DisabledIndependenceReturnsOneForEveryAlternative) {
    const std::vector alternatives{
          alternative(0.0, 30.0, 30.0, 2.0)
        , alternative(0.0, 30.0, 30.0, 2.0)
    };

    const auto result = compute_split_independences(
        independence_config(false),
        alternatives
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->size(), 2u);
    EXPECT_DOUBLE_EQ((*result)[0].get(), 1.0);
    EXPECT_DOUBLE_EQ((*result)[1].get(), 1.0);
}

TEST(SplitIndependence, SingleAlternativeIsFullyIndependent) {
    const std::vector alternatives{
        alternative(0.0, 30.0, 30.0, 2.0)
    };

    const auto result = compute_split_independences(
        independence_config(),
        alternatives
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->size(), 1u);
    EXPECT_DOUBLE_EQ((*result)[0].get(), 1.0);
}

TEST(SplitIndependence, IdenticalAlternativesReduceIndependenceSymmetrically) {
    const std::vector alternatives{
          alternative(0.0, 30.0, 30.0, 2.0)
        , alternative(0.0, 30.0, 30.0, 2.0)
    };

    const auto result = compute_split_independences(
        independence_config(),
        alternatives
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->size(), 2u);
    EXPECT_NEAR((*result)[0].get(), 0.5, 1e-15);
    EXPECT_NEAR((*result)[1].get(), 0.5, 1e-15);
}

TEST(SplitIndependence, TemporalSeparationRemovesInfluence) {
    const std::vector alternatives{
          alternative(0.0, 30.0, 30.0, 2.0)
        , alternative(120.0, 150.0, 30.0, 2.0)
    };

    const auto result = compute_split_independences(
        independence_config(),
        alternatives
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->size(), 2u);
    EXPECT_DOUBLE_EQ((*result)[0].get(), 1.0);
    EXPECT_DOUBLE_EQ((*result)[1].get(), 1.0);
}

TEST(SplitIndependence, SuperiorComparedConnectionExertsStrongerInfluence) {
    auto config = independence_config();
    config.higher_perceived_journey_time_scale = Dimless{ 30.0 };
    config.lower_perceived_journey_time_scale = Dimless{ 10.0 };
    config.higher_fare_scale = Dimless{ 5.0 };
    config.lower_fare_scale = Dimless{ 2.0 };

    const auto inferior = alternative(0.0, 40.0, 40.0, 4.0);
    const auto superior = alternative(0.0, 30.0, 30.0, 2.0);

    const auto influence_on_inferior = split_connection_influence(
        config,
        inferior,
        superior
    );
    const auto influence_on_superior = split_connection_influence(
        config,
        superior,
        inferior
    );

    ASSERT_TRUE(influence_on_inferior.has_value())
        << influence_on_inferior.error().to_string();
    ASSERT_TRUE(influence_on_superior.has_value())
        << influence_on_superior.error().to_string();
    EXPECT_GT(influence_on_inferior->get(), influence_on_superior->get());
}

TEST(SplitIndependence, RejectsNonFiniteAlternativeAndInvalidConfig) {
    const std::vector invalid_alternatives{
        alternative(
              0.0
            , 30.0
            , std::numeric_limits<double>::quiet_NaN()
            , 2.0
        )
    };
    EXPECT_FALSE(compute_split_independences(
        independence_config(),
        invalid_alternatives
    ).has_value());

    auto invalid_config = independence_config();
    invalid_config.temporal_similarity_scale = Dimless{ 0.0 };
    const std::vector alternatives{
        alternative(0.0, 30.0, 30.0, 2.0)
    };
    EXPECT_FALSE(compute_split_independences(
        invalid_config,
        alternatives
    ).has_value());
}

}  // namespace timetable::domain::assignment
