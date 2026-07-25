#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/residual/query.hpp"

namespace timetable::domain::assignment {
namespace {

    ResidualReachability query_reachability() {
        auto destination = DestinationResidualReachability{};
        destination.reachable_states.insert(ResidualReachabilityKey{
              .current_physical    = endpoint_key(ZoneId{ 2 })
            , .phase               = SearchBranchPhase::Completed
            , .remaining_transfers = TransferCount{ 0 }
        });
        destination.reachable_states.insert(ResidualReachabilityKey{
              .current_physical    = endpoint_key(StopId{ 10 })
            , .phase               = SearchBranchPhase::BeforeFirstBoarding
            , .remaining_transfers = TransferCount{ 1 }
        });
        destination.reachable_states.insert(ResidualReachabilityKey{
              .current_physical    = endpoint_key(StopId{ 11 })
            , .phase               = SearchBranchPhase::AfterTimedRide
            , .remaining_transfers = TransferCount{ 0 }
        });

        auto reachability = ResidualReachability{};
        reachability.destinations.emplace(ZoneId{ 2 }, destination);
        return reachability;
    }

}  // namespace

TEST(ResidualQuery, MissingDestinationIsConservativelyFeasible) {
    const auto decision = evaluate_residual_reachability(
          ResidualReachability{}
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(ZoneId{ 1 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::AtOrigin
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 0 }
    );

    EXPECT_TRUE(decision.feasible);
}

TEST(ResidualQuery, ClassifiesReachableStateAsFeasible) {
    const auto decision = evaluate_residual_reachability(
          query_reachability()
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(StopId{ 11 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::AfterTimedRide
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );

    EXPECT_TRUE(decision.feasible);
}

TEST(ResidualQuery, ClassifiesTransferBudgetBeforePhase) {
    const auto decision = evaluate_residual_reachability(
          query_reachability()
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(StopId{ 10 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::BeforeFirstBoarding
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );

    EXPECT_FALSE(decision.feasible);
    EXPECT_EQ(decision.rejection_reason, ReachabilityRejectionReason::TransferBudget);
}

TEST(ResidualQuery, ClassifiesKnownEndpointWrongPhase) {
    const auto decision = evaluate_residual_reachability(
          query_reachability()
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(StopId{ 11 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::AfterTransferWalk
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );

    EXPECT_FALSE(decision.feasible);
    EXPECT_EQ(decision.rejection_reason, ReachabilityRejectionReason::Phase);
}

TEST(ResidualQuery, ClassifiesUnknownEndpointAsUnreachableDestination) {
    const auto decision = evaluate_residual_reachability(
          query_reachability()
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(StopId{ 99 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::AfterTimedRide
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );

    EXPECT_FALSE(decision.feasible);
    EXPECT_EQ(decision.rejection_reason, ReachabilityRejectionReason::UnreachableDestination);
}

TEST(ResidualQuery, CompletedStateMustBeAtDestinationZone) {
    const auto wrong_zone = evaluate_residual_reachability(
          query_reachability()
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(ZoneId{ 1 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::Completed
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );
    EXPECT_FALSE(wrong_zone.feasible);
    EXPECT_EQ(wrong_zone.rejection_reason, ReachabilityRejectionReason::Phase);

    const auto destination_zone = evaluate_residual_reachability(
          query_reachability()
        , RelaxedSuffixState{
              .current_physical    = endpoint_key(ZoneId{ 2 })
            , .destination         = ZoneId{ 2 }
            , .phase               = SearchBranchPhase::Completed
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );
    EXPECT_TRUE(destination_zone.feasible);
}

}  // namespace timetable::domain::assignment
