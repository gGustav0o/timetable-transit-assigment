#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search/cost/capacity_index.hpp"
#include "timetable/domain/assignment/search/cost/validation.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] VehicleJourneyItemKey item_key(
          TripId       trip
        , std::int64_t from_index
    ) {
        return VehicleJourneyItemKey{
              .trip       = trip
            , .from_index = RoutePosition{ from_index }
        };
    }

    [[nodiscard]] VehicleJourneyItemCapacity capacity(
          TripId       trip
        , std::int64_t from_index
        , double       total_capacity = 100.0
    ) {
        return VehicleJourneyItemCapacity{
              .key            = item_key(trip, from_index)
            , .total_capacity = total_capacity
            , .seat_capacity  = total_capacity
        };
    }

    [[nodiscard]] VehicleJourneyItemLoad load(
          IntervalId   interval
        , TripId       trip
        , std::int64_t from_index
        , double       passengers
    ) {
        return VehicleJourneyItemLoad{
              .key = VehicleJourneyItemLoadKey{
                    .interval = interval
                  , .item     = item_key(trip, from_index)
              }
            , .passengers = passengers
        };
    }

    [[nodiscard]] VehicleJourneyItemLoadState load_state(
        std::vector<VehicleJourneyItemLoad> items
    ) {
        return VehicleJourneyItemLoadState{ .items = std::move(items) };
    }

    [[nodiscard]] VehicleJourneyItemCapacitySet capacity_set(
        std::vector<VehicleJourneyItemCapacity> items
    ) {
        return VehicleJourneyItemCapacitySet{ .items = std::move(items) };
    }

    [[nodiscard]] mathfp::Expected<SearchCapacityCostConfig> capacity_config(
          VehicleJourneyItemLoadState   loads
        , VehicleJourneyItemCapacitySet capacities
    ) {
        SearchCapacityCostConfig config{
              .volume_capacity_ratio = Dimless{ 0.25 }
            , .penalty_policy        = CapacityPenaltyPolicy::VolumeCapacityRatio
            , .load_state            = std::move(loads)
            , .capacity_set          = std::move(capacities)
        };

        MATHFP_TRY_LET(
              SearchCapacityCostIndex
            , index
            , build_search_capacity_cost_index(
                  config.load_state
                , config.capacity_set
                , config.penalty_policy
            )
        );
        config.index = std::move(index);
        return config;
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

}  // namespace

TEST(SearchCostValidation, AcceptsKnownModesAndRejectsUnknownMode) {
    EXPECT_TRUE(validate_search_cost_mode(SearchCostMode::BaseOnly).has_value());
    EXPECT_TRUE(validate_search_cost_mode(SearchCostMode::CapacityAware).has_value());
    EXPECT_FALSE(validate_search_cost_mode(static_cast<SearchCostMode>(77)).has_value());
}

TEST(SearchCostValidation, BaseOnlyCapacityConfigMustBeEmpty) {
    EXPECT_TRUE(validate_search_capacity_cost_config(
          SearchCapacityCostConfig{}
        , SearchCostMode::BaseOnly
    ).has_value());

    EXPECT_FALSE(validate_search_capacity_cost_config(
          SearchCapacityCostConfig{
              .volume_capacity_ratio = Dimless{ 0.25 }
          }
        , SearchCostMode::BaseOnly
    ).has_value());
}

TEST(SearchCostValidation, CapacityAwareConfigRequiresPositiveFactorCapacityAndValidIndex) {
    const auto config = capacity_config(
          load_state({
              load(IntervalId{ 3 }, TripId{ 11 }, 2, 50.0)
          })
        , capacity_set({
              capacity(TripId{ 11 }, 2)
          })
    );
    ASSERT_TRUE(config.has_value()) << config.error().to_string();

    EXPECT_TRUE(validate_search_capacity_cost_config(
          *config
        , SearchCostMode::CapacityAware
    ).has_value());

    auto missing_factor = *config;
    missing_factor.volume_capacity_ratio = Dimless{ 0.0 };
    EXPECT_FALSE(validate_search_capacity_cost_config(
          missing_factor
        , SearchCostMode::CapacityAware
    ).has_value());

    auto empty_capacity = SearchCapacityCostConfig{
        .volume_capacity_ratio = Dimless{ 0.25 }
    };
    EXPECT_FALSE(validate_search_capacity_cost_config(
          empty_capacity
        , SearchCostMode::CapacityAware
    ).has_value());

    auto broken_index = *config;
    broken_index.index.capacity_positions.clear();
    EXPECT_FALSE(validate_search_capacity_cost_config(
          broken_index
        , SearchCostMode::CapacityAware
    ).has_value());
}

TEST(SearchCostValidation, ContextValidatesModeImpedanceFareScaleAndCapacity) {
    const auto config = capacity_config(
          load_state({})
        , capacity_set({
              capacity(TripId{ 11 }, 2)
          })
    );
    ASSERT_TRUE(config.has_value()) << config.error().to_string();

    EXPECT_TRUE(validate_search_cost_context(
        SearchCostContext{
              .mode       = SearchCostMode::CapacityAware
            , .impedance  = impedance_weights()
            , .fare_scale = 4.0
            , .capacity   = *config
        }
    ).has_value());

    auto invalid_impedance = impedance_weights();
    invalid_impedance.fare = Dimless{ -1.0 };
    EXPECT_FALSE(validate_search_cost_context(
        SearchCostContext{
              .mode       = SearchCostMode::CapacityAware
            , .impedance  = invalid_impedance
            , .fare_scale = 4.0
            , .capacity   = *config
        }
    ).has_value());

    EXPECT_FALSE(validate_search_cost_context(
        SearchCostContext{
              .mode       = SearchCostMode::CapacityAware
            , .impedance  = impedance_weights()
            , .fare_scale = std::numeric_limits<double>::infinity()
            , .capacity   = *config
        }
    ).has_value());
}

TEST(SearchCostValidation, ComponentsValidateBaseMetricsAndCapacityExposure) {
    EXPECT_TRUE(validate_search_cost_components(
        SearchCostComponents{
              .base              = impedance_components()
            , .capacity_exposure = CapacityExposure{ .equivalent_time = Time{ 8.0 } }
        }
    ).has_value());

    auto invalid_base = impedance_components();
    invalid_base.fare = -1.0;
    EXPECT_FALSE(validate_search_cost_components(
        SearchCostComponents{
              .base              = invalid_base
            , .capacity_exposure = CapacityExposure{ .equivalent_time = Time{ 8.0 } }
        }
    ).has_value());

    EXPECT_FALSE(validate_search_cost_components(
        SearchCostComponents{
              .base              = impedance_components()
            , .capacity_exposure = CapacityExposure{ .equivalent_time = Time{ -1.0 } }
        }
    ).has_value());
}

}  // namespace timetable::domain::assignment
