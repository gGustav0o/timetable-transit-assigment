#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/impedance_transform.hpp"
#include "timetable/domain/numeric.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] SplitImpedanceTransformPolicy transform_policy(
          bool enabled
        , double t
    ) {
        return SplitImpedanceTransformPolicy{
            .config = SplitImpedanceTransformConfig{
                  .boxcox_transform_enabled = enabled
                , .boxcox_t = Dimless{ t }
            }
        };
    }

    [[nodiscard]] SplitImpedancePolicy impedance_policy() {
        return SplitImpedancePolicy{
              .q_time = Dimless{ 2.0 }
            , .q_departure = Dimless{ 3.0 }
            , .q_fare = Dimless{ 5.0 }
            , .temporal_utility = SplitTemporalUtilityPolicy{
                  .basis = SplitReferenceTimeBasis::Departure
                , .weights = TemporalUtilityWeights{
                      .early_departure = Dimless{ 7.0 }
                    , .late_departure = Dimless{ 11.0 }
                  }
            }
        };
    }

    [[nodiscard]] TimeInterval interval(double start, double end) {
        return TimeInterval{
              .id = IntervalId{ 1 }
            , .start = Time{ start }
            , .end = Time{ end }
        };
    }

    [[nodiscard]] SplitImpedanceAlternativeView alternative(
          double departure
        , double arrival
        , double perceived_journey_time
        , double fare
    ) {
        return SplitImpedanceAlternativeView{
              .departure_time = Time{ departure }
            , .arrival_time = Time{ arrival }
            , .perceived_journey_time = perceived_journey_time
            , .fare = fare
        };
    }

}  // namespace

TEST(SplitImpedance, TemporalUtilityIsZeroInsideInterval) {
    const auto result = split_temporal_utility(
          alternative(12.0, 20.0, 30.0, 2.0)
        , interval(10.0, 15.0)
        , impedance_policy().temporal_utility
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_DOUBLE_EQ(*result, 0.0);
}

TEST(SplitImpedance, TemporalUtilityUsesEarlyDepartureDeviation) {
    const auto result = split_temporal_utility(
          alternative(8.0, 20.0, 30.0, 2.0)
        , interval(10.0, 15.0)
        , impedance_policy().temporal_utility
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_DOUBLE_EQ(*result, 14.0);
}

TEST(SplitImpedance, TemporalUtilityUsesLateArrivalDeviationWhenBasisIsArrival) {
    auto policy = impedance_policy();
    policy.temporal_utility.basis = SplitReferenceTimeBasis::Arrival;

    const auto result = split_temporal_utility(
          alternative(8.0, 18.0, 30.0, 2.0)
        , interval(10.0, 15.0)
        , policy.temporal_utility
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_DOUBLE_EQ(*result, 33.0);
}

TEST(SplitImpedance, ComputesPaperRawImpedanceFormula) {
    const auto result = compute_split_impedance(
          alternative(8.0, 20.0, 30.0, 2.0)
        , interval(10.0, 15.0)
        , impedance_policy()
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_DOUBLE_EQ(result->get(), 2.0 * 30.0 + 3.0 * 14.0 + 5.0 * 2.0);
}

TEST(SplitImpedance, HigherJourneyTimeAndFareIncreaseRawImpedance) {
    const auto policy = impedance_policy();
    const auto demand_interval = interval(10.0, 15.0);
    const auto base = compute_split_impedance(
          alternative(12.0, 20.0, 30.0, 2.0)
        , demand_interval
        , policy
    );
    const auto higher_pjt = compute_split_impedance(
          alternative(12.0, 20.0, 31.0, 2.0)
        , demand_interval
        , policy
    );
    const auto higher_fare = compute_split_impedance(
          alternative(12.0, 20.0, 30.0, 3.0)
        , demand_interval
        , policy
    );

    ASSERT_TRUE(base.has_value()) << base.error().to_string();
    ASSERT_TRUE(higher_pjt.has_value()) << higher_pjt.error().to_string();
    ASSERT_TRUE(higher_fare.has_value()) << higher_fare.error().to_string();
    EXPECT_GT(higher_pjt->get(), base->get());
    EXPECT_GT(higher_fare->get(), base->get());
}

TEST(SplitImpedance, RejectsInvalidRawImpedanceInputs) {
    EXPECT_FALSE(compute_split_impedance(
          alternative(12.0, 20.0, std::numeric_limits<double>::infinity(), 2.0)
        , interval(10.0, 15.0)
        , impedance_policy()
    ).has_value());

    EXPECT_FALSE(compute_split_impedance(
          alternative(12.0, 20.0, 30.0, -1.0)
        , interval(10.0, 15.0)
        , impedance_policy()
    ).has_value());

    auto bad_policy = impedance_policy();
    bad_policy.q_time = Dimless{ std::numeric_limits<double>::quiet_NaN() };
    EXPECT_FALSE(compute_split_impedance(
          alternative(12.0, 20.0, 30.0, 2.0)
        , interval(10.0, 15.0)
        , bad_policy
    ).has_value());
}

TEST(SplitImpedance, DisabledTransformReturnsPositiveImpedance) {
    const auto result = apply_impedance_transform(
          SplitRawImpedance{ 7.0 }
        , transform_policy(false, 0.0)
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_DOUBLE_EQ(result->get(), 7.0);
}

TEST(SplitImpedance, DisabledTransformAppliesPositiveStabilityFloor) {
    const auto result = apply_impedance_transform(
          SplitRawImpedance{ 0.0 }
        , transform_policy(false, 0.0)
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_DOUBLE_EQ(result->get(), numeric::positive_stability_floor());
}

TEST(SplitImpedance, BoxCoxZeroParameterUsesLog) {
    const auto result = apply_impedance_transform(
          SplitRawImpedance{ 9.0 }
        , transform_policy(true, 0.0)
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_NEAR(result->get(), std::log(9.0), 1e-15);
}

TEST(SplitImpedance, BoxCoxNonZeroParameterUsesPaperFormula) {
    const auto result = apply_impedance_transform(
          SplitRawImpedance{ 9.0 }
        , transform_policy(true, 0.5)
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_NEAR(result->get(), (std::pow(9.0, 0.5) - 1.0) / 0.5, 1e-15);
}

TEST(SplitImpedance, RejectsNonFiniteInputAndParameter) {
    EXPECT_FALSE(apply_impedance_transform(
          SplitRawImpedance{ std::numeric_limits<double>::infinity() }
        , transform_policy(false, 0.0)
    ).has_value());

    EXPECT_FALSE(apply_impedance_transform(
          SplitRawImpedance{ -1.0 }
        , transform_policy(false, 0.0)
    ).has_value());

    EXPECT_FALSE(apply_impedance_transform(
          SplitRawImpedance{ 1.0 }
        , transform_policy(true, std::numeric_limits<double>::quiet_NaN())
    ).has_value());
}

}  // namespace timetable::domain::assignment
