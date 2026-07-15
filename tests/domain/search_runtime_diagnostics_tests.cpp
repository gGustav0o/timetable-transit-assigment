#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"

namespace timetable::domain::assignment::runtime {
namespace {

TEST(SearchRuntimeDiagnostics, ReachabilityRejectionCountersAreValueConsistent) {
    TaskSearchStats stats;

    add_reachability_rejection(stats, ReachabilityRejectionReason::Phase);
    add_reachability_rejection(stats, ReachabilityRejectionReason::TransferBudget);
    add_reachability_rejection(stats, ReachabilityRejectionReason::UnreachableDestination);

    EXPECT_EQ(stats.rejected_reachability, 3u);
    EXPECT_EQ(stats.reachability_rejections.phase, 1u);
    EXPECT_EQ(stats.reachability_rejections.transfer_budget, 1u);
    EXPECT_EQ(stats.reachability_rejections.unreachable_destination, 1u);
    EXPECT_TRUE(validate_reachability_rejection_stats(stats).has_value());
}

TEST(SearchRuntimeDiagnostics, ReachabilityValidationRejectsUnsummedDiagnostics) {
    TaskSearchStats stats;
    stats.rejected_reachability = 2u;
    stats.reachability_rejections.phase = 1u;

    EXPECT_FALSE(validate_reachability_rejection_stats(stats).has_value());
}

TEST(SearchRuntimeDiagnostics, SuffixLowerBoundRejectionCountersAreValueConsistent) {
    TaskSearchStats stats;

    add_suffix_lower_bound_rejection(
          stats
        , SuffixLowerBoundRejectionReason::ExactDominance
    );
    add_suffix_lower_bound_rejection(
          stats
        , SuffixLowerBoundRejectionReason::ToleranceImpedance
    );
    add_suffix_lower_bound_rejection(
          stats
        , SuffixLowerBoundRejectionReason::ToleranceJourneyTime
    );
    add_suffix_lower_bound_rejection(
          stats
        , SuffixLowerBoundRejectionReason::ToleranceTransfers
    );

    EXPECT_EQ(stats.rejected_suffix_lower_bound, 4u);
    EXPECT_EQ(stats.suffix_lower_bound_rejections.exact_dominance, 1u);
    EXPECT_EQ(stats.suffix_lower_bound_rejections.tolerance_impedance, 1u);
    EXPECT_EQ(stats.suffix_lower_bound_rejections.tolerance_journey_time, 1u);
    EXPECT_EQ(stats.suffix_lower_bound_rejections.tolerance_transfers, 1u);
    EXPECT_TRUE(validate_suffix_lower_bound_rejection_stats(stats).has_value());
}

TEST(SearchRuntimeDiagnostics, SuffixLowerBoundValidationRejectsUnsummedDiagnostics) {
    TaskSearchStats stats;
    stats.rejected_suffix_lower_bound = 3u;
    stats.suffix_lower_bound_rejections.exact_dominance = 1u;
    stats.suffix_lower_bound_rejections.tolerance_impedance = 1u;

    EXPECT_FALSE(validate_suffix_lower_bound_rejection_stats(stats).has_value());
}

}  // namespace
}  // namespace timetable::domain::assignment::runtime
