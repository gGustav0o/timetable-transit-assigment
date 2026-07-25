#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/residual/lower_bound.hpp"

namespace timetable::domain::assignment {
namespace {

    ResidualReverseGraph single_ride_suffix_graph() {
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

    ResidualReverseGraph transfer_suffix_graph() {
        return ResidualReverseGraph{
              .walk_predecessors_by_node = {
                    {
                          endpoint_key(ZoneId{ 3 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(StopId{ 30 })
                                  , .run_time    = Time{ 2.0 }
                              }
                          }
                    }
              }
            , .timed_predecessors_by_node = {
                    {
                          endpoint_key(StopId{ 30 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(StopId{ 20 })
                                  , .run_time    = Time{ 7.0 }
                              }
                          }
                    }
                  , {
                          endpoint_key(StopId{ 20 })
                        , {
                              ResidualReverseEdge{
                                    .predecessor = endpoint_key(StopId{ 10 })
                                  , .run_time    = Time{ 6.0 }
                              }
                          }
                    }
              }
        };
    }

}  // namespace

TEST(ResidualLowerBound, BuildsJourneyTimeLowerBoundForRelaxedSuffix) {
    const auto lower_bounds = build_residual_suffix_lower_bounds(
          single_ride_suffix_graph()
        , ZoneId{ 2 }
        , TransferCount{ 0 }
        , SearchImpedance{}
        , 1.0
    );

    const auto origin = lower_bounds.find(ResidualReachabilityKey{
          .current_physical    = endpoint_key(ZoneId{ 1 })
        , .phase               = SearchBranchPhase::AtOrigin
        , .remaining_transfers = TransferCount{ 0 }
    });
    ASSERT_NE(origin, lower_bounds.end());
    EXPECT_EQ(origin->second.journey_time, Time{ 17.0 });
    EXPECT_EQ(origin->second.transfers, TransferCount{ 0 });
}

TEST(ResidualLowerBound, TransferLowerBoundCountsLaterTimedBoarding) {
    const auto lower_bounds = build_residual_suffix_lower_bounds(
          transfer_suffix_graph()
        , ZoneId{ 3 }
        , TransferCount{ 1 }
        , SearchImpedance{}
        , 1.0
    );

    const auto after_first_ride = lower_bounds.find(ResidualReachabilityKey{
          .current_physical    = endpoint_key(StopId{ 20 })
        , .phase               = SearchBranchPhase::AfterTimedRide
        , .remaining_transfers = TransferCount{ 1 }
    });
    ASSERT_NE(after_first_ride, lower_bounds.end());
    EXPECT_EQ(after_first_ride->second.journey_time, Time{ 9.0 });
    EXPECT_EQ(after_first_ride->second.transfers, TransferCount{ 1 });
}

}  // namespace timetable::domain::assignment
