#include <limits>
#include <numeric>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/split_allocation.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] TimeInterval interval() {
        return TimeInterval{
              .id = IntervalId{ 1 }
            , .start = Time{ 0.0 }
            , .end = Time{ 100.0 }
        };
    }

    [[nodiscard]] SplitShareAlternativeView alternative(
          double perceived_journey_time
        , double independence = 1.0
    ) {
        return SplitShareAlternativeView{
              .impedance = SplitImpedanceAlternativeView{
                    .departure_time = Time{ 10.0 }
                  , .arrival_time = Time{ 20.0 }
                  , .perceived_journey_time = perceived_journey_time
                  , .fare = 0.0
              }
            , .independence = SplitIndependenceWeight{ independence }
        };
    }

    [[nodiscard]] SplitShareAllocationPolicy allocation_policy() {
        return SplitShareAllocationPolicy{
              .impedance = SplitImpedancePolicy{
                    .q_time = Dimless{ 1.0 }
                  , .q_departure = Dimless{ 0.0 }
                  , .q_fare = Dimless{ 0.0 }
                  , .temporal_utility = SplitTemporalUtilityPolicy{
                        .basis = SplitReferenceTimeBasis::Departure
                  }
              }
            , .impedance_transform = SplitImpedanceTransformPolicy{
                  .config = SplitImpedanceTransformConfig{
                        .boxcox_transform_enabled = false
                      , .boxcox_t = Dimless{ 0.0 }
                  }
              }
            , .choice_model = SplitChoiceModelConfig{
                  .model = SplitChoiceModel::Logit
                , .exponent = Dimless{ 1.0 }
              }
            , .probability = ProbabilityPolicy{}
        };
    }

    [[nodiscard]] double sum_passengers(
        const std::vector<SplitPassengerMass>& passengers
    ) {
        return std::accumulate(
              passengers.begin()
            , passengers.end()
            , 0.0
            , [](double total, SplitPassengerMass value) {
                  return total + value.get();
              }
        );
    }

}  // namespace

TEST(SplitShareAllocation, EqualAlternativesSplitDemandEqually) {
    const std::vector<SplitShareAlternativeView> alternatives{
          alternative(10.0)
        , alternative(10.0)
    };

    const auto result = compute_split_share_allocation(
          alternatives
        , interval()
        , SplitDemandMass{ 40.0 }
        , allocation_policy()
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    ASSERT_EQ(result->allocation.probabilities.size(), 2u);
    EXPECT_NEAR(result->allocation.probabilities[0].get(), 0.5, 1e-15);
    EXPECT_NEAR(result->allocation.probabilities[1].get(), 0.5, 1e-15);
    EXPECT_DOUBLE_EQ(result->allocation.passengers[0].get(), 20.0);
    EXPECT_DOUBLE_EQ(result->allocation.passengers[1].get(), 20.0);
    EXPECT_DOUBLE_EQ(sum_passengers(result->allocation.passengers), 40.0);
    EXPECT_DOUBLE_EQ(result->split_impedances[0].get(), 10.0);
    EXPECT_DOUBLE_EQ(result->split_impedances[1].get(), 10.0);
}

TEST(SplitShareAllocation, HigherImpedanceLowersProbabilityWhenIndependenceIsEqual) {
    const std::vector<SplitShareAlternativeView> alternatives{
          alternative(10.0)
        , alternative(11.0)
    };

    const auto result = compute_split_share_allocation(
          alternatives
        , interval()
        , SplitDemandMass{ 1.0 }
        , allocation_policy()
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_GT(
          result->allocation.probabilities[0].get()
        , result->allocation.probabilities[1].get()
    );
}

TEST(SplitShareAllocation, HigherIndependenceRaisesProbabilityWhenImpedanceIsEqual) {
    const std::vector<SplitShareAlternativeView> alternatives{
          alternative(10.0, 2.0)
        , alternative(10.0, 1.0)
    };

    const auto result = compute_split_share_allocation(
          alternatives
        , interval()
        , SplitDemandMass{ 1.0 }
        , allocation_policy()
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_NEAR(result->allocation.probabilities[0].get(), 2.0 / 3.0, 1e-15);
    EXPECT_NEAR(result->allocation.probabilities[1].get(), 1.0 / 3.0, 1e-15);
}

TEST(SplitShareAllocation, EmptyAlternativesProduceEmptyAllocation) {
    const std::vector<SplitShareAlternativeView> alternatives;

    const auto result = compute_split_share_allocation(
          alternatives
        , interval()
        , SplitDemandMass{ 5.0 }
        , allocation_policy()
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_TRUE(result->independences.empty());
    EXPECT_TRUE(result->split_impedances.empty());
    EXPECT_TRUE(result->allocation.probabilities.empty());
    EXPECT_TRUE(result->allocation.passengers.empty());
}

TEST(SplitShareAllocation, RejectsInvalidInputs) {
    const std::vector<SplitShareAlternativeView> valid{ alternative(10.0) };
    const std::vector<SplitShareAlternativeView> invalid_impedance{
        alternative(std::numeric_limits<double>::infinity())
    };
    const std::vector<SplitShareAlternativeView> invalid_independence{
        alternative(10.0, 0.0)
    };

    EXPECT_FALSE(compute_split_share_allocation(
          valid
        , interval()
        , SplitDemandMass{ -1.0 }
        , allocation_policy()
    ).has_value());
    EXPECT_FALSE(compute_split_share_allocation(
          invalid_impedance
        , interval()
        , SplitDemandMass{ 1.0 }
        , allocation_policy()
    ).has_value());
    EXPECT_FALSE(compute_split_share_allocation(
          invalid_independence
        , interval()
        , SplitDemandMass{ 1.0 }
        , allocation_policy()
    ).has_value());
}

}  // namespace timetable::domain::assignment
