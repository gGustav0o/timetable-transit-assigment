#include <gtest/gtest.h>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/od_day/supply_graph.hpp"

namespace timetable::domain::assignment {

TEST(OdDaySupplyGraph, BuildsStructuralEdgesFromPreprocessedConnectionSegments) {
    const auto network = test_support::od_day_preprocessed_network();
    ASSERT_TRUE(network.has_value()) << network.error().message();

    const auto graph = build_day_level_supply_search_graph(*network);
    ASSERT_TRUE(validate_production_day_level_supply_graph(graph).has_value());

    const auto profile = summarize_day_level_supply_search_profile(graph);
    EXPECT_EQ(profile.access_walk_edges, 1u);
    EXPECT_EQ(profile.transfer_walk_edges, 0u);
    EXPECT_EQ(profile.egress_walk_edges, 1u);
    EXPECT_EQ(profile.access_walk_labels, 1u);
    EXPECT_EQ(profile.transfer_walk_labels, 0u);
    EXPECT_EQ(profile.egress_walk_labels, 1u);
    EXPECT_EQ(profile.ride_edges, 1u);
    EXPECT_EQ(profile.ride_support_labels, 1u);

    ASSERT_EQ(graph.graph.nodes.size(), 4u);
    ASSERT_EQ(graph.graph.edges.size(), 3u);

    const auto access_it = graph.access_walks_by_from.find(endpoint_key(ZoneId{ 1 }));
    ASSERT_NE(access_it, graph.access_walks_by_from.end());
    ASSERT_EQ(access_it->second.size(), 1u);
    EXPECT_EQ(access_it->second.front().structural_leg.kind, ConnectionLegKind::AccessWalk);
    EXPECT_EQ(access_it->second.front().support_labels.size(), 1u);

    const auto ride_it = graph.rides_by_from.find(endpoint_key(StopId{ 10 }));
    ASSERT_NE(ride_it, graph.rides_by_from.end());
    ASSERT_EQ(ride_it->second.size(), 1u);
    EXPECT_EQ(ride_it->second.front().structural_leg.kind, ConnectionLegKind::Ride);
    EXPECT_EQ(ride_it->second.front().support_labels.size(), 1u);

    const auto ride_edge_ref = ride_it->second.front().edge;
    const auto& ride_edge = graph.graph.edges.at(static_cast<std::size_t>(ride_edge_ref.get()));
    EXPECT_EQ(ride_edge.kind, DayLevelSupplyEdgeKind::Ride);
    EXPECT_EQ(ride_edge.timed_support.connection_count, 1u);
    ASSERT_TRUE(ride_edge.timed_support.representative_connection_segment.has_value());
    EXPECT_EQ(*ride_edge.timed_support.representative_connection_segment, ConnectionSegmentId{ 1 });

    const auto egress_it = graph.egress_walks_by_from.find(endpoint_key(StopId{ 11 }));
    ASSERT_NE(egress_it, graph.egress_walks_by_from.end());
    ASSERT_EQ(egress_it->second.size(), 1u);
    EXPECT_EQ(egress_it->second.front().structural_leg.kind, ConnectionLegKind::EgressWalk);
    EXPECT_EQ(egress_it->second.front().support_labels.size(), 1u);
}

}  // namespace timetable::domain::assignment
