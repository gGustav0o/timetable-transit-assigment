#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/capacity_aware/penalty.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemKey item_key(
        std::int64_t from_index = 2
    ) {
        return VehicleJourneyItemKey{
              .trip = TripId{ 11 }
            , .from_index = RoutePosition{ from_index }
        };
    }

    [[nodiscard]] VehicleJourneyItemCapacity capacity(
          double total_capacity
        , VehicleJourneyItemKey key = item_key()
    ) {
        return VehicleJourneyItemCapacity{
              .key = key
            , .total_capacity = total_capacity
            , .seat_capacity = total_capacity
        };
    }

    [[nodiscard]] VehicleJourneyItemLoad load(
          double passengers
        , VehicleJourneyItemKey key = item_key()
    ) {
        return VehicleJourneyItemLoad{
              .key = VehicleJourneyItemLoadKey{
                    .interval = IntervalId{ 3 }
                  , .item = key
              }
            , .passengers = passengers
        };
    }

}  // namespace

TEST(CapacityAwarePenalty, ComputesCapacityRatioFromScalarAndLoadRow) {
    const auto scalar_ratio = capacity_ratio(25.0, capacity(100.0));
    const auto load_ratio = capacity_ratio(load(50.0), capacity(100.0));

    ASSERT_TRUE(scalar_ratio.has_value()) << scalar_ratio.error().to_string();
    ASSERT_TRUE(load_ratio.has_value()) << load_ratio.error().to_string();
    EXPECT_DOUBLE_EQ(mathfp::units::as_dimless(*scalar_ratio), 0.25);
    EXPECT_DOUBLE_EQ(mathfp::units::as_dimless(*load_ratio), 0.5);
}

TEST(CapacityAwarePenalty, RejectsInvalidCapacityRatioInputs) {
    EXPECT_FALSE(capacity_ratio(-1.0, capacity(100.0)).has_value());
    EXPECT_FALSE(capacity_ratio(1.0, capacity(0.0)).has_value());
    EXPECT_FALSE(capacity_ratio(
          load(1.0, item_key(4))
        , capacity(100.0, item_key(5))
    ).has_value());
}

TEST(CapacityAwarePenalty, AppliesPenaltyPolicies) {
    const auto volume = capacity_penalty(
          CapacityPenaltyPolicy::VolumeCapacityRatio
        , Dimless{ 1.25 }
    );
    const auto excess = capacity_penalty(
          CapacityPenaltyPolicy::ExcessVolumeCapacityRatio
        , Dimless{ 1.25 }
    );
    const auto under_capacity_excess = capacity_penalty(
          CapacityPenaltyPolicy::ExcessVolumeCapacityRatio
        , Dimless{ 0.75 }
    );

    ASSERT_TRUE(volume.has_value()) << volume.error().to_string();
    ASSERT_TRUE(excess.has_value()) << excess.error().to_string();
    ASSERT_TRUE(under_capacity_excess.has_value())
        << under_capacity_excess.error().to_string();
    EXPECT_DOUBLE_EQ(mathfp::units::as_dimless(*volume), 1.25);
    EXPECT_DOUBLE_EQ(mathfp::units::as_dimless(*excess), 0.25);
    EXPECT_DOUBLE_EQ(mathfp::units::as_dimless(*under_capacity_excess), 0.0);
}

TEST(CapacityAwarePenalty, CapacityExposureAndAdjustedCostsAreFiniteNonNegative) {
    const auto exposure = make_capacity_exposure(Time{ 30.0 });
    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();

    const auto pjt = capacity_adjusted_perceived_journey_time(
          100.0
        , *exposure
        , Dimless{ 0.5 }
    );
    const auto split_impedance = capacity_adjusted_split_impedance(
          200.0
        , *exposure
        , Dimless{ 2.0 }
        , Dimless{ 0.5 }
    );

    ASSERT_TRUE(pjt.has_value()) << pjt.error().to_string();
    ASSERT_TRUE(split_impedance.has_value()) << split_impedance.error().to_string();
    EXPECT_DOUBLE_EQ(*pjt, 115.0);
    EXPECT_DOUBLE_EQ(*split_impedance, 230.0);
}

TEST(CapacityAwarePenalty, RejectsInvalidExposureAndAdjustmentInputs) {
    EXPECT_FALSE(make_capacity_exposure(Time{ -1.0 }).has_value());

    const auto exposure = make_capacity_exposure(Time{ 10.0 });
    ASSERT_TRUE(exposure.has_value()) << exposure.error().to_string();
    EXPECT_FALSE(capacity_adjusted_perceived_journey_time(
          std::numeric_limits<double>::quiet_NaN()
        , *exposure
        , Dimless{ 1.0 }
    ).has_value());
    EXPECT_FALSE(capacity_adjusted_split_impedance(
          1.0
        , *exposure
        , Dimless{ -1.0 }
        , Dimless{ 1.0 }
    ).has_value());
}

}  // namespace timetable::domain::assignment
