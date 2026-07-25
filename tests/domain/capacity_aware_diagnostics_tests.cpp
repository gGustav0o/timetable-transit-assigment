#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/capacity_aware/diagnostics.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] CapacityAwareSplitDiagnostics enabled_split_diagnostics() {
        return CapacityAwareSplitDiagnostics{
              .enabled                 = true
            , .iterations              = 3
            , .converged               = true
            , .max_load_delta          = 0.25
            , .max_relative_load_delta = 0.01
        };
    }

}  // namespace

TEST(CapacityAwareDiagnostics, DisabledSplitDiagnosticsMustBeEmpty) {
    const auto disabled = make_capacity_aware_split_disabled_diagnostics();
    EXPECT_TRUE(validate_capacity_aware_split_diagnostics(disabled).has_value());

    EXPECT_FALSE(validate_capacity_aware_split_diagnostics(
        CapacityAwareSplitDiagnostics{
              .enabled = false
            , .iterations = 1
            , .converged = false
            , .max_load_delta = 0.0
            , .max_relative_load_delta = 0.0
        }
    ).has_value());
}

TEST(CapacityAwareDiagnostics, SplitDiagnosticsRejectInvalidScalars) {
    EXPECT_FALSE(validate_capacity_aware_split_diagnostics(
        CapacityAwareSplitDiagnostics{
              .enabled = true
            , .iterations = -1
            , .converged = false
            , .max_load_delta = 0.0
            , .max_relative_load_delta = 0.0
        }
    ).has_value());
    EXPECT_FALSE(validate_capacity_aware_split_diagnostics(
        CapacityAwareSplitDiagnostics{
              .enabled = true
            , .iterations = 1
            , .converged = false
            , .max_load_delta = std::numeric_limits<double>::quiet_NaN()
            , .max_relative_load_delta = 0.0
        }
    ).has_value());
}

TEST(CapacityAwareDiagnostics, BuildsEnabledAssignmentDiagnosticsFromSplitDiagnostics) {
    const auto diagnostics = make_capacity_aware_assignment_diagnostics(
          true
        , true
        , enabled_split_diagnostics()
        , Dimless{ 2.0 }
        , CapacityPenaltyPolicy::ExcessVolumeCapacityRatio
    );

    ASSERT_TRUE(diagnostics.has_value()) << diagnostics.error().to_string();
    EXPECT_TRUE(diagnostics->capacity_aware_enabled);
    EXPECT_TRUE(diagnostics->capacity_aware_search_enabled);
    EXPECT_EQ(diagnostics->iterations, 3);
    EXPECT_TRUE(diagnostics->converged);
    EXPECT_DOUBLE_EQ(diagnostics->max_load_delta, 0.25);
    EXPECT_DOUBLE_EQ(mathfp::units::as_dimless(diagnostics->used_factor), 2.0);
    EXPECT_EQ(diagnostics->penalty_policy, CapacityPenaltyPolicy::ExcessVolumeCapacityRatio);
}

TEST(CapacityAwareDiagnostics, AssignmentDiagnosticsRejectInconsistentState) {
    EXPECT_FALSE(make_capacity_aware_assignment_diagnostics(
          false
        , false
        , enabled_split_diagnostics()
        , Dimless{ 0.0 }
        , CapacityPenaltyPolicy::VolumeCapacityRatio
    ).has_value());

    EXPECT_FALSE(validate_capacity_aware_assignment_diagnostics(
        CapacityAwareAssignmentDiagnostics{
              .capacity_aware_enabled = false
            , .capacity_aware_search_enabled = true
            , .iterations = 0
            , .converged = false
            , .max_load_delta = 0.0
            , .used_factor = Dimless{ 0.0 }
            , .penalty_policy = CapacityPenaltyPolicy::VolumeCapacityRatio
        }
    ).has_value());
}

TEST(CapacityAwareDiagnostics, DisabledAssignmentDiagnosticsAreEmpty) {
    const auto diagnostics = make_capacity_aware_assignment_disabled_diagnostics(
        CapacityPenaltyPolicy::VolumeCapacityRatio
    );

    EXPECT_TRUE(validate_capacity_aware_assignment_diagnostics(diagnostics).has_value());
    EXPECT_FALSE(diagnostics.capacity_aware_enabled);
    EXPECT_FALSE(diagnostics.capacity_aware_search_enabled);
    EXPECT_EQ(diagnostics.iterations, 0);
    EXPECT_FALSE(diagnostics.converged);
    EXPECT_DOUBLE_EQ(diagnostics.max_load_delta, 0.0);
    EXPECT_DOUBLE_EQ(mathfp::units::as_dimless(diagnostics.used_factor), 0.0);
}

}  // namespace timetable::domain::assignment
