#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/residual/closure.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] bool contains_state(
          const ResidualReachabilityStateSet& states
        , EndpointKey                         endpoint
        , SearchBranchPhase                   phase
        , TransferCount                       remaining_transfers
    ) {
        return states.contains(ResidualReachabilityKey{
              .current_physical    = endpoint
            , .phase               = phase
            , .remaining_transfers = remaining_transfers
        });
    }

    ResidualReverseGraph single_ride_closure_graph() {
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
              }
            , .timed_predecessors_by_node = {
                    {
                          endpoint_key(StopId{ 11 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(StopId{ 10 })
                                  , .run_time    = Time{ 8.0 }
                              }
                          }
                    }
              }
        };
    }

}  // namespace

TEST(ResidualClosure, BuildsReachableStateClosureFromDestination) {
    const auto states = build_residual_reachable_states(
          single_ride_closure_graph()
        , ZoneId{ 2 }
        , TransferCount{ 0 }
    );

    EXPECT_TRUE(contains_state(
          states
        , endpoint_key(ZoneId{ 2 })
        , SearchBranchPhase::Completed
        , TransferCount{ 0 }
    ));
    EXPECT_TRUE(contains_state(
          states
        , endpoint_key(StopId{ 11 })
        , SearchBranchPhase::AfterTimedRide
        , TransferCount{ 0 }
    ));
    EXPECT_TRUE(contains_state(
          states
        , endpoint_key(StopId{ 10 })
        , SearchBranchPhase::BeforeFirstBoarding
        , TransferCount{ 0 }
    ));
    EXPECT_TRUE(contains_state(
          states
        , endpoint_key(ZoneId{ 1 })
        , SearchBranchPhase::AtOrigin
        , TransferCount{ 0 }
    ));
}

TEST(ResidualClosure, SeedsEveryRemainingTransferBudgetAtDestination) {
    const auto states = build_residual_reachable_states(
          ResidualReverseGraph{}
        , ZoneId{ 2 }
        , TransferCount{ 2 }
    );

    EXPECT_TRUE(contains_state(
          states
        , endpoint_key(ZoneId{ 2 })
        , SearchBranchPhase::Completed
        , TransferCount{ 0 }
    ));
    EXPECT_TRUE(contains_state(
          states
        , endpoint_key(ZoneId{ 2 })
        , SearchBranchPhase::Completed
        , TransferCount{ 1 }
    ));
    EXPECT_TRUE(contains_state(
          states
        , endpoint_key(ZoneId{ 2 })
        , SearchBranchPhase::Completed
        , TransferCount{ 2 }
    ));
}

}  // namespace timetable::domain::assignment
