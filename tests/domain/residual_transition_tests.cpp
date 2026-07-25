#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/residual/transition.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] bool contains_transition(
          const std::vector<ResidualPredecessorTransition>& transitions
        , EndpointKey                                       predecessor
        , SearchBranchPhase                                 phase
        , TransferCount                                     remaining_transfers
        , ResidualTransitionKind                            kind
    ) {
        return std::any_of(
              transitions.begin()
            , transitions.end()
            , [&](const ResidualPredecessorTransition& transition) {
                return transition.predecessor.current_physical == predecessor
                    && transition.predecessor.phase == phase
                    && transition.predecessor.remaining_transfers == remaining_transfers
                    && transition.kind == kind;
            }
        );
    }

    ResidualReverseGraph residual_transition_graph() {
        return ResidualReverseGraph{
              .walk_predecessors_by_node = {
                    {
                          endpoint_key(StopId{ 10 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(ZoneId{ 1 })
                                  , .run_time    = Time{ 5.0 }
                              }
                          }
                    }
                  , {
                          endpoint_key(ZoneId{ 2 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(StopId{ 11 })
                                  , .run_time    = Time{ 4.0 }
                              }
                          }
                    }
                  , {
                          endpoint_key(StopId{ 12 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(StopId{ 11 })
                                  , .run_time    = Time{ 3.0 }
                              }
                          }
                    }
              }
            , .timed_predecessors_by_node = {
                    {
                          endpoint_key(StopId{ 11 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(StopId{ 10 })
                                  , .run_time    = Time{ 7.0 }
                              }
                          }
                    }
              }
        };
    }

}  // namespace

TEST(ResidualTransition, WalkTransitionsRespectBranchPhase) {
    const auto graph = residual_transition_graph();

    const auto access_transitions = residual_predecessor_transitions(
          graph
        , ResidualReachabilityKey{
              .current_physical    = endpoint_key(StopId{ 10 })
            , .phase               = SearchBranchPhase::BeforeFirstBoarding
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );
    ASSERT_EQ(access_transitions.size(), 1u);
    EXPECT_TRUE(contains_transition(
          access_transitions
        , endpoint_key(ZoneId{ 1 })
        , SearchBranchPhase::AtOrigin
        , TransferCount{ 0 }
        , ResidualTransitionKind::AccessWalk
    ));

    const auto egress_transitions = residual_predecessor_transitions(
          graph
        , ResidualReachabilityKey{
              .current_physical    = endpoint_key(ZoneId{ 2 })
            , .phase               = SearchBranchPhase::Completed
            , .remaining_transfers = TransferCount{ 1 }
          }
        , TransferCount{ 1 }
    );
    ASSERT_EQ(egress_transitions.size(), 1u);
    EXPECT_TRUE(contains_transition(
          egress_transitions
        , endpoint_key(StopId{ 11 })
        , SearchBranchPhase::AfterTimedRide
        , TransferCount{ 1 }
        , ResidualTransitionKind::EgressWalk
    ));
}

TEST(ResidualTransition, TimedRideEmitsFirstBoardingAndTransferPredecessors) {
    const auto graph = residual_transition_graph();

    const auto transitions = residual_predecessor_transitions(
          graph
        , ResidualReachabilityKey{
              .current_physical    = endpoint_key(StopId{ 11 })
            , .phase               = SearchBranchPhase::AfterTimedRide
            , .remaining_transfers = TransferCount{ 0 }
          }
        , TransferCount{ 1 }
    );

    ASSERT_EQ(transitions.size(), 3u);
    EXPECT_TRUE(contains_transition(
          transitions
        , endpoint_key(StopId{ 10 })
        , SearchBranchPhase::BeforeFirstBoarding
        , TransferCount{ 0 }
        , ResidualTransitionKind::FirstTimedRide
    ));
    EXPECT_TRUE(contains_transition(
          transitions
        , endpoint_key(StopId{ 10 })
        , SearchBranchPhase::AfterTimedRide
        , TransferCount{ 1 }
        , ResidualTransitionKind::TransferTimedRide
    ));
    EXPECT_TRUE(contains_transition(
          transitions
        , endpoint_key(StopId{ 10 })
        , SearchBranchPhase::AfterTransferWalk
        , TransferCount{ 1 }
        , ResidualTransitionKind::TransferTimedRide
    ));
}

TEST(ResidualTransition, TransferCostCountsOnlyTransferTimedRide) {
    const auto first_ride = ResidualPredecessorTransition{
        .predecessor = ResidualReachabilityKey{
              .current_physical    = endpoint_key(StopId{ 0 })
            , .phase               = SearchBranchPhase::BeforeFirstBoarding
            , .remaining_transfers = TransferCount{ 0 }
        },
        .run_time    = Time{},
        .kind        = ResidualTransitionKind::FirstTimedRide
    };
    const auto transfer_ride = ResidualPredecessorTransition{
        .predecessor = ResidualReachabilityKey{
              .current_physical    = endpoint_key(StopId{ 0 })
            , .phase               = SearchBranchPhase::AfterTimedRide
            , .remaining_transfers = TransferCount{ 1 }
        },
        .run_time    = Time{},
        .kind        = ResidualTransitionKind::TransferTimedRide
    };

    EXPECT_EQ(residual_transition_transfer_count(first_ride), 0.0);
    EXPECT_EQ(residual_transition_transfer_count(transfer_ride), 1.0);
}

}  // namespace timetable::domain::assignment
