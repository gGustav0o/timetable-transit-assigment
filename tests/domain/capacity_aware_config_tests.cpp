#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/capacity_aware/config.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] CapacityIterationConfig valid_iteration_config() {
        return CapacityIterationConfig{
              .max_iterations = 12
            , .absolute_load_tolerance = 0.5
            , .relative_load_tolerance = 0.01
        };
    }

}  // namespace

TEST(CapacityAwareConfig, AcceptsKnownSearchModes) {
    EXPECT_TRUE(validate_capacity_aware_search_mode(
        CapacityAwareSearchMode::Disabled
    ).has_value());
    EXPECT_TRUE(validate_capacity_aware_search_mode(
        CapacityAwareSearchMode::StoredOnly
    ).has_value());
    EXPECT_TRUE(validate_capacity_aware_search_mode(
        CapacityAwareSearchMode::Enabled
    ).has_value());
}

TEST(CapacityAwareConfig, RejectsUnknownSearchMode) {
    EXPECT_FALSE(validate_capacity_aware_search_mode(
        static_cast<CapacityAwareSearchMode>(255)
    ).has_value());
}

TEST(CapacityAwareConfig, ValidatesIterationConfig) {
    const auto config = make_capacity_iteration_config(8, 0.25, 0.05);

    ASSERT_TRUE(config.has_value()) << config.error().to_string();
    EXPECT_EQ(config->max_iterations, 8);
    EXPECT_DOUBLE_EQ(config->absolute_load_tolerance, 0.25);
    EXPECT_DOUBLE_EQ(config->relative_load_tolerance, 0.05);
}

TEST(CapacityAwareConfig, RejectsInvalidIterationConfig) {
    EXPECT_FALSE(make_capacity_iteration_config(0, 0.25, 0.05).has_value());
    EXPECT_FALSE(make_capacity_iteration_config(
          8
        , std::numeric_limits<double>::quiet_NaN()
        , 0.05
    ).has_value());
    EXPECT_FALSE(make_capacity_iteration_config(8, 0.0, 0.0).has_value());
}

TEST(CapacityAwareConfig, BuildsAssignmentConfig) {
    const auto config = make_capacity_aware_assignment_config(
          true
        , CapacityAwareSearchMode::Enabled
        , CapacityPenaltyPolicy::ExcessVolumeCapacityRatio
        , valid_iteration_config()
    );

    ASSERT_TRUE(config.has_value()) << config.error().to_string();
    EXPECT_TRUE(config->capacity_aware_split_enabled);
    EXPECT_EQ(config->search_mode, CapacityAwareSearchMode::Enabled);
    EXPECT_EQ(config->penalty_policy, CapacityPenaltyPolicy::ExcessVolumeCapacityRatio);
    EXPECT_EQ(config->iteration.max_iterations, 12);
}

TEST(CapacityAwareConfig, DisabledConfigUsesCanonicalDefaults) {
    const auto config = make_capacity_aware_assignment_disabled_config();

    EXPECT_FALSE(config.capacity_aware_split_enabled);
    EXPECT_EQ(config.search_mode, CapacityAwareSearchMode::Disabled);
    EXPECT_EQ(config.penalty_policy, CapacityPenaltyPolicy::VolumeCapacityRatio);
    EXPECT_TRUE(validate_capacity_iteration_config(config.iteration).has_value());
}

}  // namespace timetable::domain::assignment
