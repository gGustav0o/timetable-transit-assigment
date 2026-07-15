#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment {
namespace {

    SearchBranch branch(
          ZoneId                     origin
        , EndpointKey                current
        , std::optional<std::size_t> parent = std::nullopt
    ) {
        return SearchBranch{
            .trace = SearchPartialTrace{
                  .origin           = origin
                , .current_physical = current
                , .parent_branch    = parent
            }
          , .od_day_carrier = OdDayProductionCarrier{
                .path_identity = make_od_day_path_prefix(origin)
            }
        };
    }

}  // namespace

TEST(BranchArena, RetainsParentUntilLiveChildIsReleased) {
    BranchArena arena;

    const auto root = append_branch(
          arena
        , branch(ZoneId{ 1 }, endpoint_key(ZoneId{ 1 }))
    );
    const auto child = append_branch(
          arena
        , branch(ZoneId{ 1 }, endpoint_key(StopId{ 10 }), root)
    );

    EXPECT_TRUE(branch_alive(arena, root));
    EXPECT_TRUE(branch_alive(arena, child));

    release_branch_if_closed(
          arena
        , root
        , [](std::size_t) {}
    );

    EXPECT_TRUE(branch_alive(arena, root));
    EXPECT_TRUE(branch_alive(arena, child));

    release_branch_if_closed(
          arena
        , child
        , [](std::size_t) {}
    );

    EXPECT_FALSE(branch_alive(arena, child));
    EXPECT_FALSE(branch_alive(arena, root));
}

TEST(BranchArena, ReleasePayloadReceivesClosedSlotsFromLeafToRoot) {
    BranchArena arena;

    const auto root = append_branch(
          arena
        , branch(ZoneId{ 1 }, endpoint_key(ZoneId{ 1 }))
    );
    const auto child = append_branch(
          arena
        , branch(ZoneId{ 1 }, endpoint_key(StopId{ 10 }), root)
    );

    std::vector<std::size_t> released;
    release_branch_if_closed(
          arena
        , root
        , [&](std::size_t index) { released.push_back(index); }
    );
    release_branch_if_closed(
          arena
        , child
        , [&](std::size_t index) { released.push_back(index); }
    );

    ASSERT_EQ(released.size(), 2u);
    EXPECT_EQ(released[0], child);
    EXPECT_EQ(released[1], root);
}

TEST(SearchFrontierLayer, IsFifoLayerOfBranchIndices) {
    SearchFrontierLayer layer;

    layer.push_back(4u);
    layer.push_back(9u);

    ASSERT_EQ(layer.size(), 2u);
    EXPECT_EQ(layer.front(), 4u);
    layer.pop_front();
    EXPECT_EQ(layer.front(), 9u);
    layer.pop_front();
    EXPECT_TRUE(layer.empty());
}

TEST(SearchFrontier, AdvancesNextLayerToCurrentLayer) {
    SearchFrontier frontier;
    frontier.current.push_back(1u);
    frontier.next.push_back(2u);

    EXPECT_EQ(frontier.size(), 2u);
    frontier.current.pop_front();
    frontier.advance_layer();

    ASSERT_FALSE(frontier.current.empty());
    EXPECT_EQ(frontier.current.front(), 2u);
    EXPECT_TRUE(frontier.next.empty());
}

TEST(SearchBranchModel, RejectsAtOriginBranchWithOccurrenceState) {
    auto impossible = branch(ZoneId{ 1 }, endpoint_key(ZoneId{ 1 }));
    impossible.trace.current_occurrence = StopOccurrenceKey{
          .stop     = StopId{ 10 }
        , .position = RoutePosition{ 0 }
    };

    EXPECT_FALSE(validate_search_branch_phase_invariants(impossible).has_value());
}

}  // namespace timetable::domain::assignment
