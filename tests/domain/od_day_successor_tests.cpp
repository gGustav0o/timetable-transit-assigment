#include <vector>

#include <gtest/gtest.h>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/od_day/successor.hpp"

namespace timetable::domain::assignment {
namespace {

    TransferLimits transfer_limits() {
        return TransferLimits{
              .max_transfers     = TransferCount{ 3 }
            , .min_transfer_wait = Time{ 5.0 }
            , .max_transfer_wait = Time{ 20.0 }
        };
    }

    SearchBranch origin_branch() {
        return SearchBranch{
            .trace = SearchPartialTrace{
                  .origin           = ZoneId{ 1 }
                , .current_physical = endpoint_key(ZoneId{ 1 })
                , .phase            = SearchBranchPhase::AtOrigin
            }
          , .od_day_carrier = OdDayProductionCarrier{
                .path_identity = make_od_day_path_prefix(ZoneId{ 1 })
            }
        };
    }

    SearchBranch preboarding_branch() {
        return SearchBranch{
            .trace = SearchPartialTrace{
                  .origin           = ZoneId{ 1 }
                , .current_physical = endpoint_key(StopId{ 10 })
                , .phase            = SearchBranchPhase::BeforeFirstBoarding
            }
          , .od_day_carrier = OdDayProductionCarrier{
                .path_identity = make_od_day_path_prefix(ZoneId{ 1 })
            }
        };
    }

    DayLevelSupplySearchGraph day_graph_from_fixture(
        const PreprocessedNetwork& network
    ) {
        return build_day_level_supply_search_graph(network);
    }

}  // namespace

TEST(OdDaySuccessor, EmitsAccessWalkFromStructuralSupplyGraph) {
    const auto network = test_support::od_day_preprocessed_network();
    ASSERT_TRUE(network.has_value()) << network.error().message();
    const auto graph = day_graph_from_fixture(*network);

    std::vector<SearchSuccessor> successors;
    auto rejected_transfer_walks = std::size_t{ 0 };
    for_each_day_level_supply_successor(
          graph
        , *network
        , ZoneId{ 1 }
        , ActiveDestinationMembership{}
        , origin_branch()
        , transfer_limits()
        , nullptr
        , [&](SearchSuccessor successor) {
              successors.push_back(std::move(successor));
          }
        , [&](std::size_t rejected) {
              rejected_transfer_walks += rejected;
          }
    );

    ASSERT_EQ(successors.size(), 1u);
    EXPECT_EQ(successors.front().connection, ConnectionSegmentId{ 0 });
    ASSERT_TRUE(successors.front().walk_transition.has_value());
    EXPECT_EQ(successors.front().walk_transition->kind, ConnectionLegKind::AccessWalk);
    EXPECT_TRUE(successors.front().day_level_edge.has_value());
    EXPECT_FALSE(successors.front().support_envelope.has_value());
    EXPECT_EQ(rejected_transfer_walks, 0u);
}

TEST(OdDaySuccessor, EmitsRideWithTimedSupportEnvelopeFromStructuralSupplyGraph) {
    const auto network = test_support::od_day_preprocessed_network();
    ASSERT_TRUE(network.has_value()) << network.error().message();
    const auto graph = day_graph_from_fixture(*network);
    const auto first_departure_domain = SearchTimeDomain{
        .windows = {
            SearchTimeWindow{
                  .begin = Time{ 0.0 }
                , .end   = Time{ 30.0 }
            }
        }
    };

    std::vector<SearchSuccessor> successors;
    auto rejected_transfer_walks = std::size_t{ 0 };
    for_each_day_level_supply_successor(
          graph
        , *network
        , ZoneId{ 1 }
        , ActiveDestinationMembership{}
        , preboarding_branch()
        , transfer_limits()
        , &first_departure_domain
        , [&](SearchSuccessor successor) {
              successors.push_back(std::move(successor));
          }
        , [&](std::size_t rejected) {
              rejected_transfer_walks += rejected;
          }
    );

    ASSERT_EQ(successors.size(), 1u);
    EXPECT_EQ(successors.front().connection, ConnectionSegmentId{ 1 });
    EXPECT_FALSE(successors.front().walk_transition.has_value());
    EXPECT_TRUE(successors.front().day_level_edge.has_value());
    ASSERT_TRUE(successors.front().support_envelope.has_value());
    ASSERT_EQ(successors.front().support_envelope->labels.size(), 1u);
    EXPECT_EQ(successors.front().support_envelope->labels.front().connection, ConnectionSegmentId{ 1 });
    EXPECT_EQ(rejected_transfer_walks, 0u);
}

}  // namespace timetable::domain::assignment
