#include <limits>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/cost/impedance.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] ConnectionImpedanceComponents impedance_components() {
        return ConnectionImpedanceComponents{
              .in_vehicle_time    = Time{ 10.0 }
            , .access_time        = Time{ 2.0 }
            , .egress_time        = Time{ 3.0 }
            , .transfer_walk_time = Time{ 4.0 }
            , .transfer_wait_time = Time{ 5.0 }
            , .transfer_count     = TransferCount{ 2 }
            , .fare               = 12.0
        };
    }

    [[nodiscard]] SearchImpedance impedance_weights() {
        return SearchImpedance{
              .in_vehicle_time       = Dimless{ 1.0 }
            , .access_time           = Dimless{ 2.0 }
            , .egress_time           = Dimless{ 3.0 }
            , .transfer_walk_time    = Dimless{ 4.0 }
            , .transfer_wait_time    = Dimless{ 5.0 }
            , .transfer_count        = Dimless{ 6.0 }
            , .fare                  = Dimless{ 7.0 }
            , .volume_capacity_ratio = Dimless{ 0.25 }
        };
    }

}  // namespace

TEST(SearchCostImpedance, ComputesBaseGeneralizedCostFromComponents) {
    const auto impedance = base_search_impedance(
          impedance_components()
        , impedance_weights()
        , 4.0
    );

    ASSERT_TRUE(impedance.has_value()) << impedance.error().message();
    EXPECT_DOUBLE_EQ(*impedance, 97.0);
}

TEST(SearchCostImpedance, AddsCapacityExposureWithConfiguredFactor) {
    const auto impedance = capacity_adjusted_search_impedance(
          40.0
        , CapacityExposure{ .equivalent_time = Time{ 8.0 } }
        , Dimless{ 0.25 }
    );

    ASSERT_TRUE(impedance.has_value()) << impedance.error().message();
    EXPECT_DOUBLE_EQ(*impedance, 42.0);
}

TEST(SearchCostImpedance, EvaluatesBaseAndCapacityAwareContexts) {
    const auto components = SearchCostComponents{
          .base = impedance_components()
        , .capacity_exposure = CapacityExposure{ .equivalent_time = Time{ 8.0 } }
    };
    auto capacity_config = SearchCapacityCostConfig{
        .volume_capacity_ratio = Dimless{ 0.25 }
    };

    const auto base = search_impedance(
          components
        , SearchCostContext{
              .mode       = SearchCostMode::BaseOnly
            , .impedance  = impedance_weights()
            , .fare_scale = 4.0
          }
    );
    const auto capacity_adjusted = search_impedance(
          components
        , SearchCostContext{
              .mode       = SearchCostMode::CapacityAware
            , .impedance  = impedance_weights()
            , .fare_scale = 4.0
            , .capacity   = capacity_config
          }
    );

    ASSERT_TRUE(base.has_value()) << base.error().message();
    ASSERT_TRUE(capacity_adjusted.has_value()) << capacity_adjusted.error().message();
    EXPECT_DOUBLE_EQ(*base, 97.0);
    EXPECT_DOUBLE_EQ(*capacity_adjusted, 99.0);
}

TEST(SearchCostImpedance, RejectsInvalidBaseInputs) {
    auto components = impedance_components();
    components.fare = -1.0;
    EXPECT_FALSE(base_search_impedance(
          components
        , impedance_weights()
        , 4.0
    ).has_value());

    components = impedance_components();
    auto weights = impedance_weights();
    weights.fare = Dimless{ -1.0 };
    EXPECT_FALSE(base_search_impedance(
          components
        , weights
        , 4.0
    ).has_value());

    EXPECT_FALSE(base_search_impedance(
          components
        , impedance_weights()
        , std::numeric_limits<double>::infinity()
    ).has_value());
}

TEST(SearchCostImpedance, RejectsInvalidCapacityAdjustedInputs) {
    EXPECT_FALSE(capacity_adjusted_search_impedance(
          -1.0
        , CapacityExposure{ .equivalent_time = Time{ 8.0 } }
        , Dimless{ 0.25 }
    ).has_value());

    EXPECT_FALSE(capacity_adjusted_search_impedance(
          40.0
        , CapacityExposure{ .equivalent_time = Time{ -1.0 } }
        , Dimless{ 0.25 }
    ).has_value());

    EXPECT_FALSE(capacity_adjusted_search_impedance(
          40.0
        , CapacityExposure{ .equivalent_time = Time{ 8.0 } }
        , Dimless{ -0.25 }
    ).has_value());
}

}  // namespace timetable::domain::assignment
