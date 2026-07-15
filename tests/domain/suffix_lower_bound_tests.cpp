#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/pruning/suffix_lower_bound.hpp"
#include "timetable/domain/params/make/tolerances.hpp"

namespace timetable::domain::assignment {
namespace {

    SearchBranch suffix_branch() {
        return SearchBranch{
              .trace = SearchPartialTrace{
                    .origin           = ZoneId{ 1 }
                  , .current_physical = endpoint_key(StopId{ 10 })
                  , .phase            = SearchBranchPhase::AfterTimedRide
              }
            , .metrics = SearchPartialMetrics{
                    .departure       = Time{ 10.0 }
                  , .current_time    = Time{ 20.0 }
                  , .access_time     = Time{ 3.0 }
                  , .in_vehicle_time = Time{ 7.0 }
              }
            , .od_day_carrier = OdDayProductionCarrier{
                  .path_identity = make_od_day_path_prefix(ZoneId{ 1 })
              }
        };
    }

}  // namespace

TEST(SuffixLowerBoundPruning, BuildsCompletionMetricLowerBoundFromBranchAndResidualSuffix) {
    const auto lower_bound = completion_metric_lower_bound(
          suffix_branch()
        , ResidualSuffixLowerBounds{
              .journey_time = Time{ 10.0 }
            , .transfers    = TransferCount{ 1 }
            , .impedance    = 5.0
          }
        , SearchCostContext{}
    );

    ASSERT_TRUE(lower_bound.has_value()) << lower_bound.error().message();
    ASSERT_TRUE(lower_bound->departure.has_value());
    ASSERT_TRUE(lower_bound->arrival.has_value());
    EXPECT_EQ(*lower_bound->departure, Time{ 10.0 });
    EXPECT_EQ(*lower_bound->arrival, Time{ 30.0 });
    EXPECT_EQ(lower_bound->journey_time, Time{ 20.0 });
    EXPECT_EQ(lower_bound->transfers, 1.0);
    EXPECT_EQ(lower_bound->impedance, 5.0);
}

TEST(SuffixLowerBoundPruning, CompleteAlternativeCanDominateCompletionLowerBound) {
    const auto lower_bound = CompletionMetricLowerBound{
          .departure    = Time{ 10.0 }
        , .arrival      = Time{ 30.0 }
        , .journey_time = Time{ 20.0 }
        , .transfers    = 1.0
        , .impedance    = 5.0
    };

    const auto complete = CompleteConnectionMetrics{
          .departure    = Time{ 11.0 }
        , .arrival      = Time{ 29.0 }
        , .journey_time = Time{ 18.0 }
        , .transfers    = TransferCount{ 1 }
        , .impedance    = 5.0
    };

    EXPECT_TRUE(complete_connection_dominates_completion_lower_bound(
          CompleteConnectionDominanceConfig{}
        , complete
        , lower_bound
    ));
}

TEST(SuffixLowerBoundPruning, ToleranceLowerBoundReportsViolatedCoordinate) {
    auto reason = SuffixLowerBoundRejectionReason::ToleranceJourneyTime;
    const auto tolerances = make_choice_tolerances(
          Dimless{ 1.0 }
        , Dimless{ 0.0 }
        , Dimless{ 1.0 }
        , Time{ 0.0 }
        , Dimless{ 1.0 }
        , Dimless{ 0.0 }
    );
    ASSERT_TRUE(tolerances.has_value()) << tolerances.error().message();

    const auto violates = violates_complete_tolerance_lower_bound(
          CompletionMetricLowerBound{
                .journey_time = Time{ 20.0 }
              , .transfers    = 0.0
              , .impedance    = 12.0
            }
        , CompleteConnectionMetricSummary{
                .min_impedance    = 10.0
              , .min_journey_time = 20.0
              , .min_transfers    = 0.0
              , .empty            = false
            }
        , *tolerances
        , reason
    );

    EXPECT_TRUE(violates);
    EXPECT_EQ(reason, SuffixLowerBoundRejectionReason::ToleranceImpedance);
}

}  // namespace timetable::domain::assignment
