#include "timetable/domain/assignment/search/od_day/supply_graph.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
    namespace {
    struct DayLevelSupplyNodeKey final {
        EndpointKey                      endpoint{};
        std::optional<StopOccurrenceKey> occurrence{};

        auto operator<=>(const DayLevelSupplyNodeKey&) const = default;
    };

    template <typename SupportBucketMap>
    [[nodiscard]] std::size_t structural_support_edge_count(
        const SupportBucketMap& buckets
    ) noexcept {
        std::size_t total = 0;
        for (const auto& [_, supports] : buckets) {
            total += supports.size();
        }
        return total;
    }

    template <typename SupportBucketMap>
    [[nodiscard]] std::size_t structural_support_label_count(
        const SupportBucketMap& buckets
    ) noexcept {
        std::size_t total = 0;
        for (const auto& [_, supports] : buckets) {
            for (const auto& support : supports) {
                total += support.support_labels.size();
            }
        }
        return total;
    }

    [[nodiscard]] DayLevelSupplySearchProfile summarize_day_level_supply_search_profile_impl(
        const DayLevelSupplySearchGraph& graph
    ) noexcept {
        DayLevelSupplySearchProfile profile{
              .access_walk_edges    = structural_support_edge_count(graph.access_walks_by_from)
            , .transfer_walk_edges  = structural_support_edge_count(graph.transfer_walks_by_from)
            , .egress_walk_edges    = structural_support_edge_count(graph.egress_walks_by_from)
            , .access_walk_labels   = structural_support_label_count(graph.access_walks_by_from)
            , .transfer_walk_labels = structural_support_label_count(graph.transfer_walks_by_from)
            , .egress_walk_labels   = structural_support_label_count(graph.egress_walks_by_from)
        };
        for (const auto& [_, supports] : graph.rides_by_from) {
            profile.ride_edges += supports.size();
            for (const auto& support : supports) {
                profile.ride_support_labels += support.support_labels.size();
            }
        }
        return profile;
    }

    [[nodiscard]] DayLevelSupplyNodeRef day_level_node_ref(
        std::size_t index
    ) noexcept {
        return DayLevelSupplyNodeRef{ static_cast<std::int64_t>(index) };
    }

    [[nodiscard]] DayLevelSupplyEdgeRef day_level_edge_ref(
        std::size_t index
    ) noexcept {
        return DayLevelSupplyEdgeRef{ static_cast<std::int64_t>(index) };
    }

    [[nodiscard]] DayLevelSupplyNodeRef ensure_day_level_node(
          DayLevelSupplyGraph& graph
        , std::map<DayLevelSupplyNodeKey, DayLevelSupplyNodeRef>& node_refs
        , DayLevelSupplyNodeKey key
    ) {
        const auto found = node_refs.find(key);
        if (found != node_refs.end()) {
            return found->second;
        }

        const auto ref = day_level_node_ref(graph.nodes.size());
        node_refs.emplace(key, ref);
        graph.nodes.push_back(
            DayLevelSupplyNode{
                  .index      = ref
                , .endpoint   = key.endpoint
                , .occurrence = key.occurrence
            }
        );
        graph.outgoing_edges_by_node.emplace_back();
        return ref;
    }

    [[nodiscard]] DayPathLeg day_level_path_leg(
          ConnectionLegKind   kind
        , const RouteSegment& route_segment
    ) noexcept {
        if (kind == ConnectionLegKind::Ride) {
            const auto* line = line_topology_of(route_segment);
            return DayPathLeg{
                  .kind            = ConnectionLegKind::Ride
                , .route_segment   = route_segment.id
                , .physical_from   = physical_from_key(route_segment)
                , .physical_to     = physical_to_key(route_segment)
                , .occurrence_from = occurrence_key(line->from)
                , .occurrence_to   = occurrence_key(line->to)
                , .line            = line->line
                , .route           = line->route
            };
        }

        return DayPathLeg{
              .kind            = kind
            , .route_segment   = route_segment.id
            , .physical_from   = physical_from_key(route_segment)
            , .physical_to     = physical_to_key(route_segment)
            , .occurrence_from = std::nullopt
            , .occurrence_to   = std::nullopt
            , .line            = std::nullopt
            , .route           = std::nullopt
        };
    }

    [[nodiscard]] DayLevelTimedSupport day_level_timed_support_summary(
          const PreprocessedNetwork&              network
        , std::span<const ConnectionSegmentId>     connections
    ) {
        DayLevelTimedSupport summary{
              .connection_count = connections.size()
        };
        if (connections.empty()) {
            return summary;
        }

        summary.representative_connection_segment = connections.front();
        const auto& first = connection_segment_at(network, connections.front());
        const auto& first_route = route_segment_at(network, first.route_segment);
        summary.min_run_time = first_route.run_time;
        summary.representative_run_time = first_route.run_time;
        summary.representative_fare = first.fare.value_or(0.0);

        for (const auto connection_id : connections) {
            const auto& connection = connection_segment_at(network, connection_id);
            const auto& route_segment = route_segment_at(network, connection.route_segment);
            if (route_segment.run_time.value() < summary.min_run_time.value()) {
                summary.min_run_time = route_segment.run_time;
            }
        }
        return summary;
    }

    [[nodiscard]] DayLevelSupplyEdgeRef append_day_level_edge(
          DayLevelSupplyGraph& graph
        , std::map<DayLevelSupplyNodeKey, DayLevelSupplyNodeRef>& node_refs
        , DayLevelSupplyEdgeKind kind
        , DayPathLeg             structural_leg
        , DayLevelTimedSupport   timed_support
    ) {
        const auto leg = production_day_path_leg(std::move(structural_leg));
        const auto from = ensure_day_level_node(
              graph
            , node_refs
            , DayLevelSupplyNodeKey{
                  .endpoint   = leg.physical_from
                , .occurrence = leg.occurrence_from
              }
        );
        const auto to = ensure_day_level_node(
              graph
            , node_refs
            , DayLevelSupplyNodeKey{
                  .endpoint   = leg.physical_to
                , .occurrence = leg.occurrence_to
              }
        );
        const auto edge = day_level_edge_ref(graph.edges.size());
        graph.edges.push_back(
            DayLevelSupplyEdge{
                  .index          = edge
                , .kind           = kind
                , .from           = from
                , .to             = to
                , .structural_leg = leg
                , .timed_support  = timed_support
            }
        );
        graph.outgoing_edges_by_node.at(static_cast<std::size_t>(from.get())).push_back(edge);
        return edge;
    }

    void append_day_level_walk_edges(
          DayLevelSupplySearchGraph& day_graph
        , std::map<DayLevelSupplyNodeKey, DayLevelSupplyNodeRef>& node_refs
        , const PreprocessedNetwork& network
        , std::span<const ConnectionSegmentId> connections
        , DayLevelSupplyEdgeKind kind
        , std::unordered_map<EndpointKey, std::vector<DayLevelWalkSupport>>& target
    ) {
        std::map<EndpointKey, std::map<DayPathLeg, std::vector<ConnectionSegmentId>>> walk_supports;
        for (const auto connection_id : connections) {
            const auto& connection = connection_segment_at(network, connection_id);
            const auto& route_segment = route_segment_at(network, connection.route_segment);
            const auto structural_leg = production_day_path_leg(
                day_level_path_leg(
                      kind == DayLevelSupplyEdgeKind::AccessWalk
                        ? ConnectionLegKind::AccessWalk
                        : kind == DayLevelSupplyEdgeKind::TransferWalk
                            ? ConnectionLegKind::TransferWalk
                            : ConnectionLegKind::EgressWalk
                    , route_segment
                )
            );
            walk_supports[structural_leg.physical_from][structural_leg].push_back(connection_id);
        }

        for (const auto& [from, by_leg] : walk_supports) {
            auto& runtime_edges = target[from];
            runtime_edges.reserve(by_leg.size());
            for (const auto& [structural_leg, support] : by_leg) {
                const auto& representative = connection_segment_at(network, support.front());
                const auto& route_segment = route_segment_at(network, representative.route_segment);
                const auto edge = append_day_level_edge(
                      day_graph.graph
                    , node_refs
                    , kind
                    , structural_leg
                    , DayLevelTimedSupport{
                          .connection_count = support.size()
                        , .representative_connection_segment = representative.id
                        , .min_run_time = route_segment.run_time
                        , .representative_run_time = route_segment.run_time
                        , .representative_fare = representative.fare.value_or(0.0)
                      }
                );
                runtime_edges.push_back(
                    DayLevelWalkSupport{
                          .structural_leg = structural_leg
                        , .edge = edge
                        , .support_labels = support
                    }
                );
            }
        }
    }

    [[nodiscard]] DayPathLeg production_ride_path_leg(
        const RouteSegment& route_segment
    ) noexcept {
        return production_day_path_leg(
            day_level_path_leg(ConnectionLegKind::Ride, route_segment)
        );
    }

    mathfp::Expected<mathfp::Unit> validate_production_day_level_supply_graph_impl(
        const DayLevelSupplySearchGraph& day_graph
    ) {
        auto edge_kind_matches_leg = [](DayLevelSupplyEdgeKind edge_kind, ConnectionLegKind leg_kind) noexcept {
            switch (edge_kind) {
                case DayLevelSupplyEdgeKind::AccessWalk:
                    return leg_kind == ConnectionLegKind::AccessWalk;
                case DayLevelSupplyEdgeKind::Ride:
                    return leg_kind == ConnectionLegKind::Ride;
                case DayLevelSupplyEdgeKind::TransferWalk:
                    return leg_kind == ConnectionLegKind::TransferWalk;
                case DayLevelSupplyEdgeKind::EgressWalk:
                    return leg_kind == ConnectionLegKind::EgressWalk;
            }
            return false;
        };

        for (std::size_t edge_index = 0; edge_index < day_graph.graph.edges.size(); ++edge_index) {
            const auto& edge = day_graph.graph.edges[edge_index];
            if (edge.structural_leg != production_day_path_leg(edge.structural_leg)) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day supply graph edge is not in production path identity")
                        .ctx("edge_index", static_cast<std::int64_t>(edge_index))
                );
            }
            if (!edge_kind_matches_leg(edge.kind, edge.structural_leg.kind)) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day supply graph edge kind disagrees with production path leg")
                        .ctx("edge_index", static_cast<std::int64_t>(edge_index))
                );
            }
        }

        auto validate_support_buckets =
            [&](const auto& buckets, const char* empty_message, const char* mismatch_message)
                -> mathfp::Expected<mathfp::Unit> {
                for (const auto& [_, supports] : buckets) {
                    for (const auto& support : supports) {
                        if (support.support_labels.empty()) {
                            return mathfp::unexpected(mathfp::internal_error(empty_message));
                        }
                        const auto edge_index = static_cast<std::size_t>(support.edge.get());
                        if (edge_index >= day_graph.graph.edges.size()
                            || day_graph.graph.edges[edge_index].structural_leg != support.structural_leg) {
                            return mathfp::unexpected(mathfp::internal_error(mismatch_message));
                        }
                    }
                }
                return mathfp::kUnit;
            };

        MATHFP_TRY(validate_support_buckets(
              day_graph.rides_by_from
            , "OD-day ride structural edge has no timed support labels"
            , "OD-day ride support disagrees with its structural edge"
        ));
        MATHFP_TRY(validate_support_buckets(
              day_graph.access_walks_by_from
            , "OD-day access structural edge has no support labels"
            , "OD-day access support disagrees with its structural edge"
        ));
        MATHFP_TRY(validate_support_buckets(
              day_graph.transfer_walks_by_from
            , "OD-day transfer structural edge has no support labels"
            , "OD-day transfer support disagrees with its structural edge"
        ));
        MATHFP_TRY(validate_support_buckets(
              day_graph.egress_walks_by_from
            , "OD-day egress structural edge has no support labels"
            , "OD-day egress support disagrees with its structural edge"
        ));
        return mathfp::kUnit;
    }

    [[nodiscard]] DayLevelSupplySearchGraph build_day_level_supply_search_graph_impl(
        const PreprocessedNetwork& network
    ) {
        DayLevelSupplySearchGraph day_graph;
        std::map<DayLevelSupplyNodeKey, DayLevelSupplyNodeRef> node_refs;

        append_day_level_walk_edges(
              day_graph
            , node_refs
            , network
            , std::span<const ConnectionSegmentId>{
                  network.connection_index.access_walk_order.data()
                , network.connection_index.access_walk_order.size()
              }
            , DayLevelSupplyEdgeKind::AccessWalk
            , day_graph.access_walks_by_from
        );
        append_day_level_walk_edges(
              day_graph
            , node_refs
            , network
            , std::span<const ConnectionSegmentId>{
                  network.connection_index.transfer_walk_order.data()
                , network.connection_index.transfer_walk_order.size()
              }
            , DayLevelSupplyEdgeKind::TransferWalk
            , day_graph.transfer_walks_by_from
        );
        append_day_level_walk_edges(
              day_graph
            , node_refs
            , network
            , std::span<const ConnectionSegmentId>{
                  network.connection_index.egress_walk_order.data()
                , network.connection_index.egress_walk_order.size()
              }
            , DayLevelSupplyEdgeKind::EgressWalk
            , day_graph.egress_walks_by_from
        );

        std::map<EndpointKey, std::map<DayPathLeg, std::vector<ConnectionSegmentId>>> ride_supports;
        for (const auto connection_id : network.connection_index.boarding_order) {
            const auto& connection = connection_segment_at(network, connection_id);
            const auto& route_segment = route_segment_at(network, connection.route_segment);
            const auto structural_leg = production_ride_path_leg(route_segment);
            ride_supports[structural_leg.physical_from][structural_leg].push_back(connection_id);
        }

        for (const auto& [from, by_leg] : ride_supports) {
            auto& runtime_edges = day_graph.rides_by_from[from];
            runtime_edges.reserve(by_leg.size());
            for (const auto& [structural_leg, support] : by_leg) {
                const auto edge = append_day_level_edge(
                      day_graph.graph
                    , node_refs
                    , DayLevelSupplyEdgeKind::Ride
                    , structural_leg
                    , day_level_timed_support_summary(
                          network
                        , std::span<const ConnectionSegmentId>{
                              support.data()
                            , support.size()
                          }
                      )
                );
                runtime_edges.push_back(
                    DayLevelRideSupport{
                          .structural_leg = structural_leg
                        , .edge          = edge
                        , .support_labels = support
                    }
                );
            }
        }

        return day_graph;
    }


    }  // namespace

    DayLevelSupplySearchProfile summarize_day_level_supply_search_profile(
        const DayLevelSupplySearchGraph& graph
    ) noexcept {
        return summarize_day_level_supply_search_profile_impl(graph);
    }

    mathfp::Expected<mathfp::Unit> validate_production_day_level_supply_graph(
        const DayLevelSupplySearchGraph& day_graph
    ) {
        return validate_production_day_level_supply_graph_impl(day_graph);
    }

    DayLevelSupplySearchGraph build_day_level_supply_search_graph(
        const PreprocessedNetwork& network
    ) {
        return build_day_level_supply_search_graph_impl(network);
    }

}  // namespace timetable::domain::assignment
