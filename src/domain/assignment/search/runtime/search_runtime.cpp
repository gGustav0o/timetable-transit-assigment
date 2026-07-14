#include "timetable/domain/assignment/search/runtime/search_runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/od_day_path_search.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_time_domain_diagnostics.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/assignment/search/branch_state.hpp"
#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/kernel/search_kernel.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"
#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"
#include "timetable/domain/assignment/search/relations/paper_connection_relevance.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        using PartialPruningMetrics = SearchPruningMetrics;

        struct SearchCancellationToken final {
            std::atomic_bool requested{ false };
        };

        [[nodiscard]] bool search_cancelled(
            const SearchCancellationToken* token
        ) noexcept {
            return token != nullptr
                && token->requested.load(std::memory_order_acquire);
        }

        void request_search_cancellation(
            SearchCancellationToken* token
        ) noexcept {
            if (token != nullptr) {
                token->requested.store(true, std::memory_order_release);
            }
        }

        enum class ReachabilityRejectionReason : std::uint8_t {
              Phase
            , TransferBudget
            , UnreachableDestination
        };

        struct ReachabilityRejectionStats final {
            std::size_t phase{};
            std::size_t transfer_budget{};
            std::size_t unreachable_destination{};
        };

        struct WalkKindStats final {
            std::size_t access{};
            std::size_t transfer{};
            std::size_t egress{};
        };

        struct WalkLookupStats final {
            std::size_t access{};
            std::size_t transfer{};
            std::size_t egress{};
            std::size_t skipped_by_phase{};
            std::size_t skipped_by_transfer_budget{};
        };

        struct BranchPhaseStats final {
            std::size_t at_origin{};
            std::size_t before_first_boarding{};
            std::size_t after_timed_ride{};
            std::size_t after_transfer_walk{};
            std::size_t completed{};
        };

        void increment_walk_kind_stats(
              WalkKindStats&    stats
            , ConnectionLegKind kind
        ) noexcept {
            switch (kind) {
                case ConnectionLegKind::AccessWalk:
                    ++stats.access;
                    return;

                case ConnectionLegKind::TransferWalk:
                    ++stats.transfer;
                    return;

                case ConnectionLegKind::EgressWalk:
                    ++stats.egress;
                    return;

                case ConnectionLegKind::Ride:
                case ConnectionLegKind::InitialWait:
                case ConnectionLegKind::TransferWait:
                case ConnectionLegKind::FinalWait:
                    return;
            }
        }

        [[nodiscard]] std::size_t& phase_counter(
              BranchPhaseStats& stats
            , SearchBranchPhase phase
        ) noexcept {
            switch (phase) {
                case SearchBranchPhase::AtOrigin:
                    return stats.at_origin;

                case SearchBranchPhase::BeforeFirstBoarding:
                    return stats.before_first_boarding;

                case SearchBranchPhase::AfterTimedRide:
                    return stats.after_timed_ride;

                case SearchBranchPhase::AfterTransferWalk:
                    return stats.after_transfer_walk;

                case SearchBranchPhase::Completed:
                    return stats.completed;
            }

            return stats.completed;
        }

        void increment_phase_stats(
              BranchPhaseStats& stats
            , SearchBranchPhase phase
        ) noexcept {
            ++phase_counter(stats, phase);
        }

        void decrement_phase_stats(
              BranchPhaseStats& stats
            , SearchBranchPhase phase
        ) noexcept {
            auto& counter = phase_counter(stats, phase);
            if (counter > 0u) {
                --counter;
            }
        }

        [[nodiscard]] std::string format_walk_kind_stats(
            const WalkKindStats& stats
        ) {
            return fmt::format(
                  "access/transfer/egress={}/{}/{}"
                , stats.access
                , stats.transfer
                , stats.egress
            );
        }

        [[nodiscard]] std::string format_walk_lookup_stats(
            const WalkLookupStats& stats
        ) {
            return fmt::format(
                  "access/transfer/egress/skipped_phase/skipped_budget={}/{}/{}/{}/{}"
                , stats.access
                , stats.transfer
                , stats.egress
                , stats.skipped_by_phase
                , stats.skipped_by_transfer_budget
            );
        }

        [[nodiscard]] std::string format_branch_phase_stats(
            const BranchPhaseStats& stats
        ) {
            return fmt::format(
                  "origin/preboard/timed/transfer_walk/completed={}/{}/{}/{}/{}"
                , stats.at_origin
                , stats.before_first_boarding
                , stats.after_timed_ride
                , stats.after_transfer_walk
                , stats.completed
            );
        }

        enum class SuffixLowerBoundRejectionReason : std::uint8_t {
              ExactDominance
            , ToleranceImpedance
            , ToleranceJourneyTime
            , ToleranceTransfers
        };

        struct SuffixLowerBoundRejectionStats final {
            std::size_t exact_dominance{};
            std::size_t tolerance_impedance{};
            std::size_t tolerance_journey_time{};
            std::size_t tolerance_transfers{};
        };

        struct TaskSearchStats final {
            std::size_t expanded_branches{};
            std::size_t generated_successors{};
            std::size_t accepted_branches{};
            WalkLookupStats walk_lookup{};
            WalkKindStats generated_walk{};
            WalkKindStats accepted_walk{};
            std::size_t rejected_consecutive_walk{};
            std::size_t stale_frontier_skipped{};
            std::size_t c_y_removed_dominated{};
            std::size_t c_y_removed_stale{};
            std::size_t frontier_compaction_runs{};
            std::size_t frontier_compaction_removed{};
            std::size_t frontier_compaction_removed_current{};
            std::size_t frontier_compaction_removed_next{};
            std::size_t post_layer_day_path_candidates{};
            std::size_t post_layer_day_path_inserted{};
            std::size_t post_layer_day_path_representative_replaced{};
            std::size_t post_layer_day_path_supports{};
            std::size_t paper_timed_lookup_skipped_phase{};
            std::size_t paper_timed_lookup_skipped_transfer_budget{};
            std::size_t paper_timed_successor_rejected_time_domain{};
            std::size_t paper_timed_successor_rejected_same_trip{};
            std::size_t paper_timed_successor_rejected_same_line{};
            std::size_t paper_timed_successor_rejected_feasibility{};
            BranchPhaseStats accepted_branches_by_phase{};
            std::size_t rejected_time_domain{};
            std::size_t rejected_feasibility{};
            std::size_t rejected_reboarding{};
            std::size_t rejected_cycles{};
            std::size_t rejected_transfer_limit{};
            std::size_t rejected_reachability{};
            ReachabilityRejectionStats reachability_rejections{};
            std::size_t rejected_suffix_lower_bound{};
            SuffixLowerBoundRejectionStats suffix_lower_bound_rejections{};
            std::size_t rejected_dominance_or_tolerance{};
            std::size_t completed_connections{};
            std::size_t rejected_complete_admissibility{};
            std::size_t rejected_complete_dominance{};
            std::size_t removed_complete_dominated{};
            std::size_t rejected_complete_tolerance{};
            std::size_t max_current_frontier{};
            std::size_t max_next_frontier{};
            SearchPruningRuntimeStats pruning{};
        };

        [[nodiscard]] mathfp::Expected<std::map<IntervalId, const TimeInterval*>> interval_lookup(
            const InputModel& input
        ) {
            std::map<IntervalId, const TimeInterval*> lookup;
            for (const auto& interval : input.intervals) {
                if (!lookup.emplace(interval.id, &interval).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate interval id while building search tasks")
                            .ctx("interval_id", interval.id.get())
                    );
                }
            }
            return lookup;
        }

        enum class OdDaySearchComputationContract : std::uint8_t {
              IncrementalDayPathRetention
            , StructuralEdgeExpansion
            , PaperConnectionSegmentTree
        };

        [[nodiscard]] constexpr const char* to_log_token(
            OdDaySearchComputationContract contract
        ) noexcept {
            switch (contract) {
                case OdDaySearchComputationContract::IncrementalDayPathRetention:
                    return "incremental_day_path_retention";
                case OdDaySearchComputationContract::StructuralEdgeExpansion:
                    return "structural_edge_expansion";
                case OdDaySearchComputationContract::PaperConnectionSegmentTree:
                    return "paper_connection_segment_tree";
            }
            return "unknown";
        }

        struct OdDayProductionMemoryLimits final {
            std::optional<std::size_t> max_branch_slots_per_tree{};
            std::optional<std::size_t> max_live_branches_per_tree{};
            std::optional<std::size_t> max_frontier_per_tree{};
            std::optional<std::size_t> max_od_day_label_states_per_tree{};
            std::optional<std::size_t> max_retained_day_paths_per_tree{};
            std::optional<std::size_t> max_approximate_direct_bytes_per_tree{};
        };

        [[nodiscard]] std::string format_batch_interval(
            const std::optional<IntervalId>& interval
        ) {
            if (!interval.has_value()) {
                return "period";
            }
            return std::to_string(interval->get());
        }

        struct ResidualReachabilityKey final {
            EndpointKey       current_physical{};
            SearchBranchPhase phase{ SearchBranchPhase::AtOrigin };
            TransferCount     remaining_transfers;
        };

        [[nodiscard]] bool operator==(
              const ResidualReachabilityKey& lhs
            , const ResidualReachabilityKey& rhs
        ) noexcept {
            return lhs.current_physical    == rhs.current_physical
                && lhs.phase               == rhs.phase
                && lhs.remaining_transfers == rhs.remaining_transfers;
        }

        struct ResidualReachabilityKeyHash final {
            std::size_t operator()(const ResidualReachabilityKey& key) const noexcept {
                std::size_t seed = 17u;
                seed = seed * 31u + std::hash<EndpointKey>{}(key.current_physical);
                seed = seed * 31u + std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.phase));
                seed = seed * 31u + std::hash<std::int32_t>{}(key.remaining_transfers.get());
                return seed;
            }
        };

        struct ResidualReverseEdge final {
            EndpointKey predecessor{};
            Time        run_time{};
        };

        struct ResidualReverseGraph final {
            std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> walk_predecessors_by_node{};
            std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> timed_predecessors_by_node{};
        };

        struct ResidualPhysicalEdgeKey final {
            EndpointKey from{};
            EndpointKey to{};
        };

        [[nodiscard]] bool operator==(
              const ResidualPhysicalEdgeKey& lhs
            , const ResidualPhysicalEdgeKey& rhs
        ) noexcept {
            return lhs.from == rhs.from && lhs.to == rhs.to;
        }

        struct ResidualPhysicalEdgeKeyHash final {
            std::size_t operator()(const ResidualPhysicalEdgeKey& key) const noexcept {
                std::size_t seed = 17u;
                seed = seed * 31u + std::hash<EndpointKey>{}(key.from);
                seed = seed * 31u + std::hash<EndpointKey>{}(key.to);
                return seed;
            }
        };

        struct ResidualSuffixLowerBounds final {
            Time          journey_time{};
            TransferCount transfers{ TransferCount{0} };
            double        impedance{};
        };

        struct DestinationResidualReachability final {
            std::unordered_set<ResidualReachabilityKey, ResidualReachabilityKeyHash> reachable_states{};
            std::unordered_map<
                  ResidualReachabilityKey
                , ResidualSuffixLowerBounds
                , ResidualReachabilityKeyHash
            > suffix_lower_bounds{};
        };

        struct ResidualReachability final {
            std::map<ZoneId, DestinationResidualReachability> destinations{};
        };

        struct ReachabilityDecision final {
            bool                        feasible{};
            ReachabilityRejectionReason rejection_reason{ ReachabilityRejectionReason::UnreachableDestination };
        };

        struct RejectedReachabilityTask final {
            std::size_t                 task_position{};
            ReachabilityRejectionReason reason{ ReachabilityRejectionReason::UnreachableDestination };
        };

        struct RejectedSuffixLowerBoundTask final {
            std::size_t                     task_position{};
            SuffixLowerBoundRejectionReason reason{ SuffixLowerBoundRejectionReason::ToleranceImpedance };
        };

        constexpr std::size_t kTaskProgressStep    = 10;
        constexpr std::size_t kSearchHeartbeatStep = 100'000;
        constexpr std::size_t kSearchWallClockSuccessorCheckStep = 16'384;
        constexpr std::size_t kInitialTaskBranchReserve = 4'096;
        constexpr auto kSearchWallClockHeartbeatInterval =
            std::chrono::seconds{ 30 };

        const RouteSegment& route_segment_at(
              const PreprocessedNetwork& network
            , RouteSegmentId             id
        ) {
            return network.route_segments.at(static_cast<std::size_t>(id.get()));
        }

        const ConnectionSegment& connection_segment_at(
              const PreprocessedNetwork& network
            , ConnectionSegmentId        id
        ) {
            return network.connection_segments.at(static_cast<std::size_t>(id.get()));
        }

        struct DayLevelSupplyNodeKey final {
            EndpointKey                      endpoint{};
            std::optional<StopOccurrenceKey> occurrence{};

            auto operator<=>(const DayLevelSupplyNodeKey&) const = default;
        };

        struct DayLevelWalkSupport final {
            DayPathLeg                       structural_leg{};
            DayLevelSupplyEdgeRef            edge;
            std::vector<ConnectionSegmentId> support_labels{};
        };

        struct DayLevelRideSupport final {
            DayPathLeg                       structural_leg{};
            DayLevelSupplyEdgeRef            edge;
            std::vector<ConnectionSegmentId> support_labels{};
        };

        using DayLevelTimedSupportLabel = TimedSupportLabel;

        struct DayLevelSupplySearchGraph final {
            DayLevelSupplyGraph graph{};
            std::unordered_map<EndpointKey, std::vector<DayLevelWalkSupport>> access_walks_by_from{};
            std::unordered_map<EndpointKey, std::vector<DayLevelWalkSupport>> transfer_walks_by_from{};
            std::unordered_map<EndpointKey, std::vector<DayLevelWalkSupport>> egress_walks_by_from{};
            std::unordered_map<EndpointKey, std::vector<DayLevelRideSupport>> rides_by_from{};
        };

        struct DayLevelSupplySearchProfile final {
            std::size_t access_walk_edges{};
            std::size_t transfer_walk_edges{};
            std::size_t egress_walk_edges{};
            std::size_t access_walk_labels{};
            std::size_t transfer_walk_labels{};
            std::size_t egress_walk_labels{};
            std::size_t ride_edges{};
            std::size_t ride_support_labels{};
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

        [[nodiscard]] DayLevelSupplySearchProfile summarize_day_level_supply_search_profile(
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

        mathfp::Expected<mathfp::Unit> validate_production_day_level_supply_graph(
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

        [[nodiscard]] DayLevelSupplySearchGraph build_day_level_supply_search_graph(
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

        using ResidualPhysicalEdgeTimes = std::unordered_map<
              ResidualPhysicalEdgeKey
            , Time
            , ResidualPhysicalEdgeKeyHash
        >;

        void retain_min_residual_edge_time(
              ResidualPhysicalEdgeTimes& edge_times
            , ResidualPhysicalEdgeKey    key
            , Time                       run_time
        ) {
            const auto [it, inserted] = edge_times.emplace(key, run_time);
            if (!inserted && run_time.value() < it->second.value()) {
                it->second = run_time;
            }
        }

        void append_residual_reverse_edges(
              std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>>& target
            , const ResidualPhysicalEdgeTimes&                                   edge_times
        ) {
            for (const auto& [key, run_time] : edge_times) {
                target[key.to].push_back(
                    ResidualReverseEdge{
                          .predecessor = key.from
                        , .run_time    = run_time
                    }
                );
                target.try_emplace(key.from);
            }
        }

        [[nodiscard]] ResidualReachabilityKey residual_reachability_key(
            const RelaxedSuffixState& state
        ) noexcept {
            return ResidualReachabilityKey{
                  .current_physical    = state.current_physical
                , .phase               = state.phase
                , .remaining_transfers = state.remaining_transfers
            };
        }

        [[nodiscard]] ResidualReverseGraph build_residual_reverse_graph(
              std::span<const RouteSegment>      route_segments
            , std::span<const ConnectionSegment> connection_segments
        ) {
            ResidualPhysicalEdgeTimes walk_edges;
            ResidualPhysicalEdgeTimes timed_edges;

            for (const auto& segment : route_segments) {
                if (is_walk(segment)) {
                    retain_min_residual_edge_time(
                          walk_edges
                        , ResidualPhysicalEdgeKey{
                              .from = physical_from_key(segment)
                            , .to   = physical_to_key(segment)
                          }
                        , segment.run_time
                    );
                }
            }

            for (const auto& segment : connection_segments) {
                if (!is_timed_connection(segment)
                    || !segment.departure.has_value()
                    || !segment.arrival.has_value()) {
                    continue;
                }
                const auto& route_segment = route_segments[static_cast<std::size_t>(
                    segment.route_segment.get()
                )];
                retain_min_residual_edge_time(
                      timed_edges
                    , ResidualPhysicalEdgeKey{
                          .from = physical_from_key(route_segment)
                        , .to   = physical_to_key(route_segment)
                      }
                    , Time{ segment.arrival->value() - segment.departure->value() }
                );
            }

            ResidualReverseGraph graph;
            append_residual_reverse_edges(graph.walk_predecessors_by_node, walk_edges);
            append_residual_reverse_edges(graph.timed_predecessors_by_node, timed_edges);
            return graph;
        }

        [[nodiscard]] std::size_t reachability_rejection_count(
            const ReachabilityRejectionStats& stats
        ) noexcept {
            return stats.phase
                 + stats.transfer_budget
                 + stats.unreachable_destination;
        }

        [[nodiscard]] std::size_t suffix_lower_bound_rejection_count(
            const SuffixLowerBoundRejectionStats& stats
        ) noexcept {
            return stats.exact_dominance
                 + stats.tolerance_impedance
                 + stats.tolerance_journey_time
                 + stats.tolerance_transfers;
        }

        mathfp::Expected<mathfp::Unit> validate_reachability_rejection_stats(
            const TaskSearchStats& stats
        ) {
            const auto detail_count = reachability_rejection_count(stats.reachability_rejections);
            if (detail_count != stats.rejected_reachability) {
                return mathfp::unexpected(
                    mathfp::internal_error("reachability rejection diagnostics do not sum to total")
                        .ctx("total", static_cast<std::int64_t>(stats.rejected_reachability))
                        .ctx("detail", static_cast<std::int64_t>(detail_count))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_suffix_lower_bound_rejection_stats(
            const TaskSearchStats& stats
        ) {
            const auto detail_count = suffix_lower_bound_rejection_count(
                stats.suffix_lower_bound_rejections
            );
            if (detail_count != stats.rejected_suffix_lower_bound) {
                return mathfp::unexpected(
                    mathfp::internal_error("suffix lower-bound rejection diagnostics do not sum to total")
                        .ctx("total", static_cast<std::int64_t>(stats.rejected_suffix_lower_bound))
                        .ctx("detail", static_cast<std::int64_t>(detail_count))
                );
            }
            return mathfp::kUnit;
        }

        void add_reachability_rejection(
              TaskSearchStats&            stats
            , ReachabilityRejectionReason reason
        ) noexcept {
            ++stats.rejected_reachability;
            switch (reason) {
                case ReachabilityRejectionReason::Phase:
                    ++stats.reachability_rejections.phase;
                    return;
                case ReachabilityRejectionReason::TransferBudget:
                    ++stats.reachability_rejections.transfer_budget;
                    return;
                case ReachabilityRejectionReason::UnreachableDestination:
                    ++stats.reachability_rejections.unreachable_destination;
                    return;
            }
        }

        void add_suffix_lower_bound_rejection(
              TaskSearchStats&                 stats
            , SuffixLowerBoundRejectionReason reason
        ) noexcept {
            ++stats.rejected_suffix_lower_bound;
            switch (reason) {
                case SuffixLowerBoundRejectionReason::ExactDominance:
                    ++stats.suffix_lower_bound_rejections.exact_dominance;
                    return;
                case SuffixLowerBoundRejectionReason::ToleranceImpedance:
                    ++stats.suffix_lower_bound_rejections.tolerance_impedance;
                    return;
                case SuffixLowerBoundRejectionReason::ToleranceJourneyTime:
                    ++stats.suffix_lower_bound_rejections.tolerance_journey_time;
                    return;
                case SuffixLowerBoundRejectionReason::ToleranceTransfers:
                    ++stats.suffix_lower_bound_rejections.tolerance_transfers;
                    return;
            }
        }

        [[nodiscard]] bool has_reachable_state(
              const DestinationResidualReachability& destination
            , const ResidualReachabilityKey&         state
        ) noexcept {
            return destination.reachable_states.contains(state);
        }

        template <typename Fn>
        void for_each_transfer_count_up_to(TransferCount max_transfers, Fn&& fn) {
            auto remaining = TransferCount{0};
            while (remaining <= max_transfers) {
                fn(remaining);

                const auto next = bounded_next_transfer_count(remaining, max_transfers);
                if (!next.has_value()) {
                    break;
                }
                remaining = *next;
            }
        }

        [[nodiscard]] bool has_more_budget_state(
              const DestinationResidualReachability& destination
            , const ResidualReachabilityKey&         state
            , TransferCount                          max_transfers
        ) noexcept {
            auto next_remaining = bounded_next_transfer_count(
                  state.remaining_transfers
                , max_transfers
            );
            while (next_remaining.has_value()) {
                if (has_reachable_state(
                      destination
                    , ResidualReachabilityKey{
                          .current_physical    = state.current_physical
                        , .phase               = state.phase
                        , .remaining_transfers = *next_remaining
                      }
                )) {
                    return true;
                }
                next_remaining = bounded_next_transfer_count(*next_remaining, max_transfers);
            }
            return false;
        }

        [[nodiscard]] bool has_any_phase_state_at_endpoint(
              const DestinationResidualReachability& destination
            , EndpointKey                            endpoint
        ) noexcept {
            return std::any_of(
                  destination.reachable_states.begin()
                , destination.reachable_states.end()
                , [&](const ResidualReachabilityKey& state) {
                    return state.current_physical == endpoint;
                }
            );
        }

        void enqueue_reachable_state(
              DestinationResidualReachability& destination
            , std::deque<ResidualReachabilityKey>& frontier
            , ResidualReachabilityKey              state
        ) {
            if (destination.reachable_states.insert(state).second) {
                frontier.push_back(state);
            }
        }

        void enqueue_walk_predecessors(
              const ResidualReverseGraph&          graph
            , DestinationResidualReachability&      destination
            , std::deque<ResidualReachabilityKey>& frontier
            , const ResidualReachabilityKey&        state
        ) {
            const auto predecessors = graph.walk_predecessors_by_node.find(state.current_physical);
            if (predecessors == graph.walk_predecessors_by_node.end()) {
                return;
            }

            if (state.phase == SearchBranchPhase::Completed) {
                // Reverse of AfterTimedRide --egress walk--> Completed.
                for (const auto edge : predecessors->second) {
                    const auto predecessor = edge.predecessor;
                    if (predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTimedRide
                            , .remaining_transfers = state.remaining_transfers
                          }
                    );
                }
                return;
            }

            if (state.phase == SearchBranchPhase::AfterTransferWalk) {
                // Reverse of AfterTimedRide --transfer walk--> AfterTransferWalk.
                for (const auto edge : predecessors->second) {
                    const auto predecessor = edge.predecessor;
                    if (predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTimedRide
                            , .remaining_transfers = state.remaining_transfers
                          }
                    );
                }
                return;
            }

            if (state.phase == SearchBranchPhase::BeforeFirstBoarding) {
                // Reverse of AtOrigin --access walk--> BeforeFirstBoarding.
                for (const auto edge : predecessors->second) {
                    const auto predecessor = edge.predecessor;
                    if (predecessor.kind != EndpointKind::Zone) {
                        continue;
                    }
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AtOrigin
                            , .remaining_transfers = state.remaining_transfers
                          }
                    );
                }
            }
        }

        void enqueue_timed_predecessors(
              const ResidualReverseGraph&          graph
            , DestinationResidualReachability&      destination
            , std::deque<ResidualReachabilityKey>& frontier
            , const ResidualReachabilityKey&        state
            , TransferCount                         max_transfers
        ) {
            if (state.phase != SearchBranchPhase::AfterTimedRide) {
                return;
            }

            const auto predecessors = graph.timed_predecessors_by_node.find(state.current_physical);
            if (predecessors == graph.timed_predecessors_by_node.end()) {
                return;
            }

            for (const auto edge : predecessors->second) {
                const auto predecessor = edge.predecessor;
                if (predecessor.kind != EndpointKind::Stop) {
                    continue;
                }

                // First timed boarding does not count as a transfer.
                enqueue_reachable_state(
                      destination
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = predecessor
                        , .phase               = SearchBranchPhase::BeforeFirstBoarding
                        , .remaining_transfers = state.remaining_transfers
                      }
                );

                // Every later timed boarding consumes one remaining transfer.
                if (const auto next_remaining = bounded_next_transfer_count(
                      state.remaining_transfers
                    , max_transfers
                )) {
                    // Reverse of AfterTimedRide --timed ride--> AfterTimedRide.
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTimedRide
                            , .remaining_transfers = *next_remaining
                          }
                    );
                    // Reverse of AfterTransferWalk --timed ride--> AfterTimedRide.
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTransferWalk
                            , .remaining_transfers = *next_remaining
                          }
                    );
                }
            }
        }

        enum class ResidualTransitionKind : std::uint8_t {
              AccessWalk
            , TransferWalk
            , EgressWalk
            , FirstTimedRide
            , TransferTimedRide
        };

        struct ResidualPredecessorTransition final {
            ResidualReachabilityKey predecessor{};
            Time                    run_time{};
            ResidualTransitionKind  kind{ ResidualTransitionKind::AccessWalk };
        };

        template <typename Visitor>
        void for_each_residual_predecessor_transition(
              const ResidualReverseGraph&   graph
            , const ResidualReachabilityKey& state
            , TransferCount                  max_transfers
            , Visitor&&                      visit
        ) {
            auto&& visitor = visit;

            const auto walk_predecessors = graph.walk_predecessors_by_node.find(state.current_physical);
            if (walk_predecessors != graph.walk_predecessors_by_node.end()) {
                if (state.phase == SearchBranchPhase::Completed) {
                    for (const auto edge : walk_predecessors->second) {
                        if (edge.predecessor.kind != EndpointKind::Stop) {
                            continue;
                        }
                        visitor(ResidualPredecessorTransition{
                              .predecessor = ResidualReachabilityKey{
                                    .current_physical    = edge.predecessor
                                  , .phase               = SearchBranchPhase::AfterTimedRide
                                  , .remaining_transfers = state.remaining_transfers
                                }
                            , .run_time    = edge.run_time
                            , .kind        = ResidualTransitionKind::EgressWalk
                        });
                    }
                } else if (state.phase == SearchBranchPhase::AfterTransferWalk) {
                    for (const auto edge : walk_predecessors->second) {
                        if (edge.predecessor.kind != EndpointKind::Stop) {
                            continue;
                        }
                        visitor(ResidualPredecessorTransition{
                              .predecessor = ResidualReachabilityKey{
                                    .current_physical    = edge.predecessor
                                  , .phase               = SearchBranchPhase::AfterTimedRide
                                  , .remaining_transfers = state.remaining_transfers
                                }
                            , .run_time    = edge.run_time
                            , .kind        = ResidualTransitionKind::TransferWalk
                        });
                    }
                } else if (state.phase == SearchBranchPhase::BeforeFirstBoarding) {
                    for (const auto edge : walk_predecessors->second) {
                        if (edge.predecessor.kind != EndpointKind::Zone) {
                            continue;
                        }
                        visitor(ResidualPredecessorTransition{
                              .predecessor = ResidualReachabilityKey{
                                    .current_physical    = edge.predecessor
                                  , .phase               = SearchBranchPhase::AtOrigin
                                  , .remaining_transfers = state.remaining_transfers
                                }
                            , .run_time    = edge.run_time
                            , .kind        = ResidualTransitionKind::AccessWalk
                        });
                    }
                }
            }

            if (state.phase != SearchBranchPhase::AfterTimedRide) {
                return;
            }

            const auto timed_predecessors = graph.timed_predecessors_by_node.find(state.current_physical);
            if (timed_predecessors == graph.timed_predecessors_by_node.end()) {
                return;
            }

            for (const auto edge : timed_predecessors->second) {
                if (edge.predecessor.kind != EndpointKind::Stop) {
                    continue;
                }

                visitor(ResidualPredecessorTransition{
                      .predecessor = ResidualReachabilityKey{
                            .current_physical    = edge.predecessor
                          , .phase               = SearchBranchPhase::BeforeFirstBoarding
                          , .remaining_transfers = state.remaining_transfers
                        }
                    , .run_time    = edge.run_time
                    , .kind        = ResidualTransitionKind::FirstTimedRide
                });

                if (const auto next_remaining = bounded_next_transfer_count(
                      state.remaining_transfers
                    , max_transfers
                )) {
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTimedRide
                              , .remaining_transfers = *next_remaining
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::TransferTimedRide
                    });
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTransferWalk
                              , .remaining_transfers = *next_remaining
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::TransferTimedRide
                    });
                }
            }
        }

        [[nodiscard]] double residual_transition_journey_time(
            const ResidualPredecessorTransition& transition
        ) noexcept {
            return transition.run_time.value();
        }

        [[nodiscard]] double residual_transition_transfer_count(
            const ResidualPredecessorTransition& transition
        ) noexcept {
            return transition.kind == ResidualTransitionKind::TransferTimedRide ? 1.0 : 0.0;
        }

        [[nodiscard]] double residual_transition_impedance(
              const ResidualPredecessorTransition& transition
            , const SearchImpedance&               impedance
            , double                               fare_scale
        ) noexcept {
            ConnectionImpedanceComponents components{};
            switch (transition.kind) {
                case ResidualTransitionKind::AccessWalk:
                    components.access_time = transition.run_time;
                    break;
                case ResidualTransitionKind::TransferWalk:
                    components.transfer_walk_time = transition.run_time;
                    break;
                case ResidualTransitionKind::EgressWalk:
                    components.egress_time = transition.run_time;
                    break;
                case ResidualTransitionKind::FirstTimedRide:
                    components.in_vehicle_time = transition.run_time;
                    break;
                case ResidualTransitionKind::TransferTimedRide:
                    components.in_vehicle_time = transition.run_time;
                    components.transfer_count  = TransferCount{ 1 };
                    break;
            }
            return connection_impedance_value(components, impedance, fare_scale);
        }

        using ResidualDistanceMap = std::unordered_map<
              ResidualReachabilityKey
            , double
            , ResidualReachabilityKeyHash
        >;

        struct ResidualDistanceQueueItem final {
            double                  distance{};
            ResidualReachabilityKey state{};
        };

        struct ResidualDistanceQueueGreater final {
            bool operator()(
                  const ResidualDistanceQueueItem& lhs
                , const ResidualDistanceQueueItem& rhs
            ) const noexcept {
                return lhs.distance > rhs.distance;
            }
        };

        template <typename EdgeCost>
        [[nodiscard]] ResidualDistanceMap compute_residual_suffix_distances(
              const ResidualReverseGraph& graph
            , ZoneId                      destination
            , TransferCount               max_transfers
            , EdgeCost&&                  edge_cost
        ) {
            ResidualDistanceMap distances;
            std::priority_queue<
                  ResidualDistanceQueueItem
                , std::vector<ResidualDistanceQueueItem>
                , ResidualDistanceQueueGreater
            > frontier;

            const auto destination_endpoint = endpoint_key(destination);
            for_each_transfer_count_up_to(max_transfers, [&](const TransferCount remaining) {
                const auto seed = ResidualReachabilityKey{
                      .current_physical    = destination_endpoint
                    , .phase               = SearchBranchPhase::Completed
                    , .remaining_transfers = remaining
                };
                distances.emplace(seed, 0.0);
                frontier.push(ResidualDistanceQueueItem{
                      .distance = 0.0
                    , .state    = seed
                });
            });

            while (!frontier.empty()) {
                const auto item = frontier.top();
                frontier.pop();

                const auto current_it = distances.find(item.state);
                if (current_it == distances.end() || item.distance != current_it->second) {
                    continue;
                }

                for_each_residual_predecessor_transition(
                      graph
                    , item.state
                    , max_transfers
                    , [&](const ResidualPredecessorTransition& transition) {
                        const auto candidate =
                            item.distance + edge_cost(transition);
                        const auto known = distances.find(transition.predecessor);
                        if (known != distances.end() && known->second <= candidate) {
                            return;
                        }
                        distances[transition.predecessor] = candidate;
                        frontier.push(ResidualDistanceQueueItem{
                              .distance = candidate
                            , .state    = transition.predecessor
                        });
                    }
                );
            }

            return distances;
        }

        [[nodiscard]] std::unordered_map<
              ResidualReachabilityKey
            , ResidualSuffixLowerBounds
            , ResidualReachabilityKeyHash
        > build_residual_suffix_lower_bounds(
              const ResidualReverseGraph& graph
            , ZoneId                      destination
            , TransferCount               max_transfers
            , const SearchImpedance&      impedance
            , double                      fare_scale
        ) {
            const auto journey_time_distances = compute_residual_suffix_distances(
                  graph
                , destination
                , max_transfers
                , [](const ResidualPredecessorTransition& transition) {
                    return residual_transition_journey_time(transition);
                }
            );
            const auto transfer_distances = compute_residual_suffix_distances(
                  graph
                , destination
                , max_transfers
                , [](const ResidualPredecessorTransition& transition) {
                    return residual_transition_transfer_count(transition);
                }
            );
            const auto impedance_distances = compute_residual_suffix_distances(
                  graph
                , destination
                , max_transfers
                , [&](const ResidualPredecessorTransition& transition) {
                    return residual_transition_impedance(
                          transition
                        , impedance
                        , fare_scale
                    );
                }
            );

            std::unordered_map<
                  ResidualReachabilityKey
                , ResidualSuffixLowerBounds
                , ResidualReachabilityKeyHash
            > lower_bounds;
            lower_bounds.reserve(journey_time_distances.size());
            for (const auto& [state, journey_time] : journey_time_distances) {
                const auto transfers = transfer_distances.find(state);
                const auto imp       = impedance_distances.find(state);
                if (transfers == transfer_distances.end() || imp == impedance_distances.end()) {
                    continue;
                }
                lower_bounds.emplace(
                      state
                    , ResidualSuffixLowerBounds{
                          .journey_time = Time{ journey_time }
                        , .transfers    = TransferCount{
                              static_cast<std::int32_t>(transfers->second)
                          }
                        , .impedance    = imp->second
                      }
                );
            }
            return lower_bounds;
        }

        [[nodiscard]] DestinationResidualReachability build_destination_residual_reachability(
              const ResidualReverseGraph& graph
            , ZoneId                      destination
            , TransferCount               max_transfers
            , const SearchImpedance&      impedance
            , double                      fare_scale
        ) {
            DestinationResidualReachability reachability;
            std::deque<ResidualReachabilityKey> frontier;
            const auto destination_endpoint = endpoint_key(destination);

            for_each_transfer_count_up_to(max_transfers, [&](const TransferCount remaining) {
                enqueue_reachable_state(
                      reachability
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = destination_endpoint
                        , .phase               = SearchBranchPhase::Completed
                        , .remaining_transfers = remaining
                      }
                );
            });

            while (!frontier.empty()) {
                const auto state = frontier.front();
                frontier.pop_front();

                enqueue_walk_predecessors(
                      graph
                    , reachability
                    , frontier
                    , state
                );
                enqueue_timed_predecessors(
                      graph
                    , reachability
                    , frontier
                    , state
                    , max_transfers
                );
            }

            reachability.suffix_lower_bounds = build_residual_suffix_lower_bounds(
                  graph
                , destination
                , max_transfers
                , impedance
                , fare_scale
            );

            return reachability;
        }

        [[nodiscard]] ResidualReachability build_residual_reachability(
              const ResidualReverseGraph& graph
            , std::span<const SearchCompletionTarget> targets
            , TransferCount               max_transfers
            , const SearchImpedance&      impedance
            , double                      fare_scale
        ) {
            ResidualReachability reachability;

            for (const auto& target : targets) {
                const auto destination = target.destination;
                if (reachability.destinations.contains(destination)) {
                    continue;
                }
                reachability.destinations.emplace(
                      destination
                    , build_destination_residual_reachability(
                          graph
                        , destination
                        , max_transfers
                        , impedance
                        , fare_scale
                    )
                );
            }

            return reachability;
        }

        mathfp::Expected<mathfp::Unit> validate_destination_residual_reachability(
              ZoneId                                destination
            , const DestinationResidualReachability& reachability
            , TransferCount                         max_transfers
        ) {
            const auto destination_endpoint = endpoint_key(destination);

            auto remaining = TransferCount{0};
            while (remaining <= max_transfers) {
                if (!has_reachable_state(
                      reachability
                    , ResidualReachabilityKey{
                          .current_physical    = destination_endpoint
                        , .phase               = SearchBranchPhase::Completed
                        , .remaining_transfers = remaining
                      }
                )) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability misses destination completion seed")
                            .ctx("destination", destination.get())
                            .ctx("remaining_transfers", remaining.get())
                    );
                }

                const auto next = bounded_next_transfer_count(remaining, max_transfers);
                if (!next.has_value()) {
                    break;
                }
                remaining = *next;
            }

            for (const auto& state : reachability.reachable_states) {
                if (state.remaining_transfers.get() < 0
                    || state.remaining_transfers.get() > max_transfers.get()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains state outside transfer budget")
                            .ctx("destination", destination.get())
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                            .ctx("max_transfers", max_transfers.get())
                    );
                }

                if (state.phase == SearchBranchPhase::Completed
                    && state.current_physical != destination_endpoint) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains non-destination completed state")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                    );
                }

                if (state.phase == SearchBranchPhase::AtOrigin
                    && state.current_physical.kind != EndpointKind::Zone) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains at-origin state outside zone")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }

                if ((state.phase == SearchBranchPhase::BeforeFirstBoarding
                        || state.phase == SearchBranchPhase::AfterTimedRide
                        || state.phase == SearchBranchPhase::AfterTransferWalk)
                    && state.current_physical.kind != EndpointKind::Stop) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains stop-phase state outside stop")
                            .ctx("destination", destination.get())
                            .ctx("phase", static_cast<std::int64_t>(state.phase))
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }

                if (state.phase == SearchBranchPhase::Completed
                    && state.current_physical.kind != EndpointKind::Zone) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains completed state outside zone")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }

                if (const auto next_remaining = bounded_next_transfer_count(
                      state.remaining_transfers
                    , max_transfers
                )) {
                    const auto relaxed_more_budget_state = ResidualReachabilityKey{
                          .current_physical    = state.current_physical
                        , .phase               = state.phase
                        , .remaining_transfers = *next_remaining
                    };
                    if (!has_reachable_state(reachability, relaxed_more_budget_state)) {
                        return mathfp::unexpected(
                            mathfp::internal_error("residual reachability violates transfer-budget monotonicity")
                                .ctx("destination", destination.get())
                                .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                                .ctx("endpoint_id", state.current_physical.id)
                                .ctx("remaining_transfers", state.remaining_transfers.get())
                        );
                    }
                }

                const auto lower_bounds = reachability.suffix_lower_bounds.find(state);
                if (lower_bounds == reachability.suffix_lower_bounds.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability state misses suffix lower bounds")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }
                if (
                       !std::isfinite(lower_bounds->second.journey_time.value())
                    || !std::isfinite(lower_bounds->second.impedance)
                    || lower_bounds->second.journey_time.value() < 0.0
                    || lower_bounds->second.impedance < 0.0
                    || lower_bounds->second.transfers.get() < 0
                ) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual suffix lower bounds contain invalid value")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                    );
                }
            }

            for (const auto& [state, lower_bounds] : reachability.suffix_lower_bounds) {
                (void)lower_bounds;
                if (!has_reachable_state(reachability, state)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual suffix lower bounds contain unreachable state")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_residual_reachability(
              const ResidualReachability& reachability
            , TransferCount               max_transfers
        ) {
            for (const auto& [destination, destination_reachability] : reachability.destinations) {
                MATHFP_TRY(validate_destination_residual_reachability(
                      destination
                    , destination_reachability
                    , max_transfers
                ));
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] ReachabilityDecision evaluate_residual_reachability(
              const ResidualReachability& reachability
            , const RelaxedSuffixState&   state
            , TransferCount               max_transfers
        ) noexcept {
            const auto destination_it = reachability.destinations.find(state.destination);
            if (destination_it == reachability.destinations.end()) {
                return ReachabilityDecision{ .feasible = true };
            }

            if (is_completed(state.phase)) {
                const auto feasible = state.current_physical.kind == EndpointKind::Zone
                    && state.current_physical.id == state.destination.get();
                return ReachabilityDecision{
                      .feasible = feasible
                    , .rejection_reason = feasible
                        ? ReachabilityRejectionReason::UnreachableDestination
                        : ReachabilityRejectionReason::Phase
                };
            }

            const auto key = residual_reachability_key(state);
            const auto& destination_reachability = destination_it->second;
            if (has_reachable_state(destination_reachability, key)) {
                return ReachabilityDecision{ .feasible = true };
            }

            if (has_more_budget_state(destination_reachability, key, max_transfers)) {
                return ReachabilityDecision{
                      .feasible = false
                    , .rejection_reason = ReachabilityRejectionReason::TransferBudget
                };
            }

            if (has_any_phase_state_at_endpoint(destination_reachability, state.current_physical)) {
                return ReachabilityDecision{
                      .feasible = false
                    , .rejection_reason = ReachabilityRejectionReason::Phase
                };
            }

            return ReachabilityDecision{
                  .feasible = false
                , .rejection_reason = ReachabilityRejectionReason::UnreachableDestination
            };
        }

        [[nodiscard]] bool can_have_feasible_suffix(
              const ResidualReachability& reachability
            , const RelaxedSuffixState&   state
            , TransferCount               max_transfers
        ) noexcept {
            return evaluate_residual_reachability(
                  reachability
                , state
                , max_transfers
            ).feasible;
        }

        [[nodiscard]] bool can_have_feasible_suffix(
              const ResidualReachability& reachability
            , const SearchBranch&         branch
            , const SearchTask&           task
            , const TransferLimits&       limits
        ) noexcept {
            return can_have_feasible_suffix(
                  reachability
                , relaxed_suffix_state(branch, task, limits)
                , limits.max_transfers
            );
        }

        bool branch_revisits_physical(
              const BranchArena&  branches
            , const SearchBranch& branch
            , const PreprocessedNetwork& network
            , EndpointKey         next
        ) {
            if (branch.trace.current_physical == next) {
                return true;
            }

            if (branch.od_day_carrier.support_prefix != nullptr) {
                for (auto node = branch.od_day_carrier.support_prefix; node != nullptr; node = node->parent) {
                    const auto segment_id = node->segment;
                    const auto& segment = connection_segment_at(network, segment_id);
                    const auto& route_segment = route_segment_at(network, segment.route_segment);
                    if (physical_to_key(route_segment) == next) {
                        return true;
                    }
                }
                return false;
            }

            auto cursor = branch.trace.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branch_at(branches, *cursor);
                if (ancestor.trace.current_physical == next) {
                    return true;
                }
                cursor = ancestor.trace.parent_branch;
            }

            return false;
        }

        bool branch_revisits_occurrence(
              const BranchArena&  branches
            , const SearchBranch& branch
            , const PreprocessedNetwork& network
            , StopOccurrenceKey   next
        ) {
            if (branch.trace.current_occurrence.has_value() && branch.trace.current_occurrence.value() == next) {
                return true;
            }

            if (branch.od_day_carrier.support_prefix != nullptr) {
                for (auto node = branch.od_day_carrier.support_prefix; node != nullptr; node = node->parent) {
                    const auto segment_id = node->segment;
                    const auto& segment = connection_segment_at(network, segment_id);
                    if (!is_timed_connection(segment)) {
                        continue;
                    }
                    const auto& route_segment = route_segment_at(network, segment.route_segment);
                    if (occurrence_key(line_topology_of(route_segment)->to) == next) {
                        return true;
                    }
                }
                return false;
            }

            auto cursor = branch.trace.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branch_at(branches, *cursor);
                if (ancestor.trace.current_occurrence.has_value() && ancestor.trace.current_occurrence.value() == next) {
                    return true;
                }
                cursor = ancestor.trace.parent_branch;
            }

            return false;
        }

        bool first_timed_departure_allowed(
              const SearchBranch&      branch
            , const ConnectionSegment& successor
            , const SearchTimeDomain*  first_departure_domain
            , const TransferLimits&
        ) noexcept {
            if (branch.metrics.departure.has_value()) {
                return true;
            }
            if (first_departure_domain == nullptr || !successor.departure.has_value()) {
                return true;
            }
            return contains(*first_departure_domain, *successor.departure);
        }

        mathfp::Expected<PartialPruningMetrics> make_partial_pruning_metrics(
              const SearchPartialMetrics& metrics
            , const SearchCostContext&     search_cost
        ) {
            //tex:
            // Projection from incremental branch state to the paper comparison
            // vector. For the current prefix $$c_y$$:
            // $$JT(c_y)=ARR(c_y)-DEP(c_y),\qquad TT(c_y)=TWait(c_y)+TWalk(c_y).$$
            // The last coordinate is evaluated by SearchCostContext as $$IMP(c_y)$$.
            const auto journey_time = partial_journey_time(metrics);
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(metrics)
                , .capacity_exposure = metrics.capacity_exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(cost_components, search_cost)
            );

            return PartialPruningMetrics{
                  .departure    = *metrics.departure
                , .arrival      = *metrics.current_time
                , .journey_time = journey_time
                , .walk_time    = partial_walk_time(metrics)
                , .transfers    = metrics.transfers
                , .fare         = metrics.fare
                , .impedance    = impedance
            };
        }

        mathfp::Expected<PartialPruningMetrics> make_partial_pruning_metrics(
              const SearchBranch&      branch
            , const SearchCostContext& search_cost
        ) {
            return make_partial_pruning_metrics(branch.metrics, search_cost);
        }

        mathfp::Expected<PartialPruningMetrics> make_day_path_pruning_metrics(
              const SearchBranch&      branch
            , const SearchCostContext& search_cost
        ) {
            const auto& metrics = branch.metrics;
            if (metrics.departure.has_value()
                && metrics.current_time.has_value()) {
                return make_partial_pruning_metrics(metrics, search_cost);
            }

            const auto elapsed = partial_walk_time(metrics);
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(metrics)
                , .capacity_exposure = metrics.capacity_exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(cost_components, search_cost)
            );

            return PartialPruningMetrics{
                  .departure    = Time{ 0.0 }
                , .arrival      = elapsed
                , .journey_time = elapsed
                , .walk_time    = elapsed
                , .transfers    = metrics.transfers
                , .fare         = metrics.fare
                , .impedance    = impedance
            };
        }

        mathfp::Expected<PartialPruningMetrics> make_day_path_pruning_metrics(
              const SearchPartialMetrics& metrics
            , const SearchCostContext&     search_cost
        ) {
            if (metrics.departure.has_value()
                && metrics.current_time.has_value()) {
                return make_partial_pruning_metrics(metrics, search_cost);
            }

            const auto elapsed = partial_walk_time(metrics);
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(metrics)
                , .capacity_exposure = metrics.capacity_exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(cost_components, search_cost)
            );

            return PartialPruningMetrics{
                  .departure    = Time{ 0.0 }
                , .arrival      = elapsed
                , .journey_time = elapsed
                , .walk_time    = elapsed
                , .transfers    = metrics.transfers
                , .fare         = metrics.fare
                , .impedance    = impedance
            };
        }

        void insert_pruning_metrics(
              NodeMetricMap&                    known_metrics
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchNodeKey                     node
            , PartialPruningMetrics             metrics
        ) {
            auto& known = known_metrics[node];
            insert_search_pruning_metrics_in_place(
                  pruning_execution
                , known
                , std::move(metrics)
            );
        }

        void insert_pruning_metrics(
              PaperConnectionNodeMetricMap&     known_metrics
            , const SearchPruningExecutionPlan& pruning_execution
            , PaperConnectionNodeKey            node
            , PartialPruningMetrics             metrics
            , PaperConnectionLabelId            label
            , std::vector<PaperConnectionLabelId>& removed_labels
        ) {
            auto& known = known_metrics[node];
            insert_paper_node_connection_metrics(
                  pruning_execution
                , known
                , std::move(metrics)
                , label
                , removed_labels
            );
        }

        void insert_pruning_metrics(
              SearchProjectionRetention&        retention
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchNodeKey                     node
            , PartialPruningMetrics             metrics
        ) {
            insert_pruning_metrics(
                  retention.known_metrics
                , pruning_execution
                , std::move(node)
                , std::move(metrics)
            );
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const CompleteConnectionRetention& retention
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = retention.alternatives.empty()
            };
            for (const auto& alternative : retention.alternatives) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , alternative.metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , alternative.metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(alternative.metrics.transfers.get())
                );
            }
            return summary;
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const CompactCompleteConnectionRetention& retention
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = retention.metrics.empty()
            };
            for (const auto& metrics : retention.metrics) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(metrics.transfers.get())
                );
            }
            return summary;
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const DayPathRetention& retention
        ) noexcept {
            return summarize_day_path_metrics(retention);
        }

        struct CompletionMetricLowerBound final {
            std::optional<Time> departure{};
            std::optional<Time> arrival{};
            Time                journey_time{};
            double              transfers{};
            double              impedance{};
        };

        struct SuffixLowerBoundPruningDecision final {
            bool                            feasible{ true };
            SuffixLowerBoundRejectionReason rejection_reason{
                SuffixLowerBoundRejectionReason::ToleranceImpedance
            };
        };

        [[nodiscard]] mathfp::Expected<double> partial_impedance_value(
              const SearchBranch&    branch
            , const SearchCostContext& search_cost
        ) {
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(branch.metrics)
                , .capacity_exposure = branch.metrics.capacity_exposure
            };
            return search_impedance(
                  cost_components
                , search_cost
            );
        }

        [[nodiscard]] Time partial_journey_time_lower_bound(
            const SearchBranch& branch
        ) noexcept {
            if (branch.metrics.departure.has_value()) {
                return partial_journey_time(branch.metrics);
            }
            return branch.metrics.access_time;
        }

        [[nodiscard]] double suffix_capacity_impedance_lower_bound(
            const SearchCostContext& search_cost
        ) noexcept {
            switch (search_cost.mode) {
                case SearchCostMode::BaseOnly:
                    return 0.0;

                case SearchCostMode::CapacityAware:
                    /*
                     * Supported capacity penalties are non-negative for valid
                     * load/capacity ratios, and the volume-capacity weight is
                     * validated as non-negative. Therefore zero is an
                     * admissible lower bound for the unknown suffix capacity
                     * term. This may weaken suffix pruning, but cannot reject
                     * a completion that could become feasible under the full
                     * capacity-aware search impedance.
                     */
                    return 0.0;
            }

            return 0.0;
        }

        [[nodiscard]] mathfp::Expected<CompletionMetricLowerBound> completion_metric_lower_bound(
              const SearchBranch&                branch
            , const ResidualSuffixLowerBounds& suffix
            , const SearchCostContext&           search_cost
        ) {
            MATHFP_TRY_LET(
                  double
                , partial_impedance
                , partial_impedance_value(branch, search_cost)
            );
            const auto suffix_capacity_impedance =
                suffix_capacity_impedance_lower_bound(search_cost);
            return CompletionMetricLowerBound{
                  .departure   = branch.metrics.departure
                , .arrival     = branch.metrics.current_time.has_value()
                    ? std::optional<Time>{
                        Time{ branch.metrics.current_time->value() + suffix.journey_time.value() }
                    }
                    : std::nullopt
                , .journey_time = Time{
                      partial_journey_time_lower_bound(branch).value()
                    + suffix.journey_time.value()
                  }
                , .transfers    = static_cast<double>(branch.metrics.transfers.get())
                    + static_cast<double>(suffix.transfers.get())
                , .impedance    = partial_impedance
                    + suffix.impedance
                    + suffix_capacity_impedance
            };
        }

        [[nodiscard]] bool complete_connection_dominates_completion_lower_bound(
              const CompleteConnectionDominanceConfig& dominance_config
            , const CompleteConnectionMetrics&         complete
            , const CompletionMetricLowerBound&        lower_bound
        ) noexcept {
            if (!complete_connection_can_dominate(dominance_config, complete)) {
                return false;
            }
            if (!lower_bound.departure.has_value() || !lower_bound.arrival.has_value()) {
                return false;
            }

            const auto no_worse =
                   complete.departure.value() >= lower_bound.departure->value()
                && complete.arrival.value()   <= lower_bound.arrival->value()
                && complete.impedance         <= lower_bound.impedance
                && static_cast<double>(complete.transfers.get()) <= lower_bound.transfers;

            const auto strictly_better =
                   complete.departure.value() > lower_bound.departure->value()
                || complete.arrival.value()   < lower_bound.arrival->value()
                || complete.impedance         < lower_bound.impedance
                || static_cast<double>(complete.transfers.get()) < lower_bound.transfers;

            return no_worse && strictly_better;
        }

        [[nodiscard]] bool violates_complete_tolerance_lower_bound(
              const CompletionMetricLowerBound&     lower_bound
            , const CompleteConnectionMetricSummary& summary
            , const ChoiceTolerances&               tolerances
            , SuffixLowerBoundRejectionReason&      reason
        ) noexcept {
            if (summary.empty) {
                return false;
            }

            const auto impedance_bound =
                  mathfp::units::as_dimless(tolerances.imp_mult)
                * summary.min_impedance
                + mathfp::units::as_dimless(tolerances.imp_add);
            if (lower_bound.impedance > impedance_bound) {
                reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
                return true;
            }

            const auto journey_time_bound =
                  mathfp::units::as_dimless(tolerances.jt_mult)
                * summary.min_journey_time
                + mathfp::units::as_dimless(tolerances.jt_add);
            if (lower_bound.journey_time.value() > journey_time_bound) {
                reason = SuffixLowerBoundRejectionReason::ToleranceJourneyTime;
                return true;
            }

            const auto transfer_bound =
                  mathfp::units::as_dimless(tolerances.nt_mult)
                * summary.min_transfers
                + mathfp::units::as_dimless(tolerances.nt_add);
            if (lower_bound.transfers > transfer_bound) {
                reason = SuffixLowerBoundRejectionReason::ToleranceTransfers;
                return true;
            }

            return false;
        }

        [[nodiscard]] mathfp::Expected<SuffixLowerBoundPruningDecision> evaluate_suffix_lower_bound_pruning(
              const SearchBranch&                branch
            , ZoneId                             destination
            , const ResidualReachability&        reachability
            , const CompleteConnectionRetention& complete_retention
            , const SearchParams&                params
            , const SearchCostContext&           search_cost
            , const ChoiceConfig&                choice_config
            , const CompleteConnectionDominanceConfig& dominance_config
        ) {
            if (complete_retention.alternatives.empty()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto destination_it = reachability.destinations.find(destination);
            if (destination_it == reachability.destinations.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto state = residual_reachability_key(
                relaxed_suffix_state(branch, destination, params.transfers)
            );
            const auto lower_bound_it = destination_it->second.suffix_lower_bounds.find(state);
            if (lower_bound_it == destination_it->second.suffix_lower_bounds.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            MATHFP_TRY_LET(
                  CompletionMetricLowerBound
                , lower_bound
                , completion_metric_lower_bound(
                      branch
                    , lower_bound_it->second
                    , search_cost
                )
            );

            for (const auto& complete : complete_retention.alternatives) {
                if (complete_connection_dominates_completion_lower_bound(
                      dominance_config
                    , complete.metrics
                    , lower_bound
                )) {
                    return SuffixLowerBoundPruningDecision{
                          .feasible = false
                        , .rejection_reason = SuffixLowerBoundRejectionReason::ExactDominance
                    };
                }
            }

            if (choice_config.rollout_stage != ChoiceRolloutStage::ExactAndApproximate) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            auto reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
            if (violates_complete_tolerance_lower_bound(
                  lower_bound
                , summarize_complete_metrics(complete_retention)
                , params.choice_tolerances
                , reason
            )) {
                return SuffixLowerBoundPruningDecision{
                      .feasible = false
                    , .rejection_reason = reason
                };
            }

            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        [[nodiscard]] mathfp::Expected<SuffixLowerBoundPruningDecision> evaluate_suffix_lower_bound_pruning(
              const SearchBranch&                       branch
            , ZoneId                                    destination
            , const ResidualReachability&               reachability
            , const CompactCompleteConnectionRetention& complete_retention
            , const SearchParams&                       params
            , const SearchCostContext&                  search_cost
            , const ChoiceConfig&                       choice_config
            , const CompleteConnectionDominanceConfig&  dominance_config
        ) {
            if (complete_retention.metrics.empty()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto destination_it = reachability.destinations.find(destination);
            if (destination_it == reachability.destinations.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto state = residual_reachability_key(
                relaxed_suffix_state(branch, destination, params.transfers)
            );
            const auto lower_bound_it = destination_it->second.suffix_lower_bounds.find(state);
            if (lower_bound_it == destination_it->second.suffix_lower_bounds.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            MATHFP_TRY_LET(
                  CompletionMetricLowerBound
                , lower_bound
                , completion_metric_lower_bound(
                      branch
                    , lower_bound_it->second
                    , search_cost
                )
            );

            for (const auto& complete : complete_retention.metrics) {
                if (complete_connection_dominates_completion_lower_bound(
                      dominance_config
                    , complete
                    , lower_bound
                )) {
                    return SuffixLowerBoundPruningDecision{
                          .feasible = false
                        , .rejection_reason = SuffixLowerBoundRejectionReason::ExactDominance
                    };
                }
            }

            if (choice_config.rollout_stage != ChoiceRolloutStage::ExactAndApproximate) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            auto reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
            if (violates_complete_tolerance_lower_bound(
                  lower_bound
                , summarize_complete_metrics(complete_retention)
                , params.choice_tolerances
                , reason
            )) {
                return SuffixLowerBoundPruningDecision{
                      .feasible = false
                    , .rejection_reason = reason
                };
            }

            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        std::optional<std::pair<std::size_t, std::size_t>> timed_bucket_range(
              const preprocessing::ConnectionSegmentIndex& index
            , StopOccurrenceKey                            from
        ) {
            const auto bucket = preprocessing::find_bucket(index.timed_buckets, from);
            if (!bucket) {
                return std::nullopt;
            }

            const auto i = *bucket;
            return std::pair<std::size_t, std::size_t>{
                  index.timed_offsets[i]
                , index.timed_offsets[i + 1]
            };
        }

        template <typename Visitor>
        void for_each_boarding_successor_in_window(
              const PreprocessedNetwork& network
            , std::size_t                start
            , std::size_t                end
            , SearchTimeWindow           window
            , Visitor&&                  visit
        ) {
            auto&& visitor = visit;
            const auto departures_begin =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(start);
            const auto departures_end =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(end);
            const auto begin = std::lower_bound(
                  departures_begin
                , departures_end
                , window.begin
                , [](const Time& lhs, const Time& rhs) {
                    return lhs.value() < rhs.value();
                }
            );

            auto idx = start + static_cast<std::size_t>(begin - departures_begin);
            for (auto it = begin; it != departures_end; ++it, ++idx) {
                if (it->value() > window.end.value()) {
                    break;
                }
                visitor(network.connection_index.boarding_order[idx]);
            }
        }

        template <typename Visitor>
        void for_each_timed_successor(
              const PreprocessedNetwork& network
            , EndpointKey                physical_from
            , std::optional<Time>        current_time
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
            , Visitor&&                  visit
        ) {
            //tex:
            // Timed successors from physical stop $$x$$ are traversed through
            // prebuilt departure buckets. For transfers the scanned interval is
            // $$[ARR(c_x)+MINTWT,\ ARR(c_x)+MAXTWT]$$; for the first boarding it
            // is the configured service-day departure domain. No route/trip table
            // scan is needed inside the tree loop.
            auto&& visitor = visit;
            if (physical_from.kind != EndpointKind::Stop) {
                return;
            }

            const auto bucket = preprocessing::find_bucket(
                  network.connection_index.boarding_stop_buckets
                , StopId{ physical_from.id }
            );
            if (!bucket) {
                return;
            }

            const auto bucket_index = *bucket;
            const auto start        = network.connection_index.boarding_offsets[bucket_index];
            const auto end          = network.connection_index.boarding_offsets[bucket_index + 1];
            if (start >= end) {
                return;
            }

            if (!current_time.has_value()) {
                if (first_departure_domain != nullptr) {
                    for (const auto& window : first_departure_domain->windows) {
                        for_each_boarding_successor_in_window(
                              network
                            , start
                            , end
                            , window
                            , visitor
                        );
                    }
                    return;
                }
                for (std::size_t i = start; i < end; ++i) {
                    visitor(network.connection_index.boarding_order[i]);
                }
                return;
            }

            const auto earliest = Time{
                current_time->value() + limits.min_transfer_wait.value()
            };
            const auto latest = Time{
                current_time->value() + limits.max_transfer_wait.value()
            };
            const auto departures_begin =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(start);
            const auto departures_end =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(end);
            const auto begin = std::lower_bound(
                  departures_begin
                , departures_end
                , earliest
                , [](const Time& lhs, const Time& rhs) {
                    return lhs.value() < rhs.value();
                }
            );

            auto idx = start + static_cast<std::size_t>(begin - departures_begin);
            for (auto it = begin; it != departures_end; ++it, ++idx) {
                if (it->value() > latest.value()) {
                    break;
                }
                visitor(network.connection_index.boarding_order[idx]);
            }
        }

        bool is_same_line_transfer_candidate(
              const SearchBranch&      branch
            , const ConnectionSegment& successor
            , const RouteSegment&      successor_route_segment
        ) noexcept {
            if (!branch.trace.last_timed_segment || !branch.trace.last_timed_route_segment) {
                return false;
            }
            if (!is_timed_connection(successor)) {
                return false;
            }
            if (!same_line(*branch.trace.last_timed_route_segment, successor_route_segment)) {
                return false;
            }
            return successor.trip != branch.trace.last_timed_segment->trip;
        }

        bool is_repeated_stop_reboarding_case(
              const SearchBranch&      branch
            , const ConnectionSegment& successor
            , const RouteSegment&      successor_route_segment
        ) noexcept {
            if (!is_same_line_transfer_candidate(branch, successor, successor_route_segment)) {
                return false;
            }
            const auto* current_line   = line_topology_of(*branch.trace.last_timed_route_segment);
            const auto* successor_line = line_topology_of(successor_route_segment);
            if (!current_line || !successor_line) {
                return false;
            }
            if (current_line->route != successor_line->route) {
                return false;
            }
            if (current_line->to.stop != successor_line->from.stop) {
                return false;
            }
            if (!branch.trace.last_timed_segment->to_index.has_value() || !successor.from_index.has_value()) {
                return false;
            }
            /*
             * The paper allows same-line reboarding only on loop lines when an
             * earlier service trip saves time at the loop intersection. In the
             * occurrence-aware route model this comparison is meaningful only
             * inside one route pattern; the boarded vehicle is reached at a later
             * occurrence of the same physical stop. The route-position axis must
             * not wrap from the maximal occurrence to the minimal one.
             */
            return branch.trace.last_timed_segment->to_index.value() < successor.from_index.value();
        }

        std::optional<Time> same_trip_continuation_arrival(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , const RouteSegment&        successor_route_segment
        ) noexcept {
            if (!branch.trace.last_timed_segment || !branch.trace.last_timed_route_segment) {
                return std::nullopt;
            }
            if (
                   !branch.trace.last_timed_segment->trip    .has_value()
                || !branch.trace.last_timed_segment->to_index.has_value()
                || !successor.to_index                    .has_value()
            ) {
                return std::nullopt;
            }

            const auto current_stop           = occurrence_key(line_topology_of(*branch.trace.last_timed_route_segment)->to);
            const auto desired_route_to_index = successor.to_index.value();
            const auto range                  = timed_bucket_range(network.connection_index, current_stop);
            if (!range) {
                return std::nullopt;
            }

            const auto [start, end] = *range;
            for (std::size_t i = start; i < end; ++i) {
                const auto  connection_id              = network.connection_index.timed_order[i];
                const auto& continuation               = connection_segment_at(network, connection_id);
                const auto& continuation_route_segment = route_segment_at(
                      network
                    , continuation.route_segment
                );
                const auto* continuation_line = line_topology_of(continuation_route_segment);
                const auto* current_line = line_topology_of(*branch.trace.last_timed_route_segment);
                if (!continuation_line || !current_line) {
                    continue;
                }
                if (continuation_line->line != current_line->line
                    || continuation_line->route != current_line->route) {
                    continue;
                }
                if (continuation.trip != branch.trace.last_timed_segment->trip) {
                    continue;
                }
                if (continuation.from_index != branch.trace.last_timed_segment->to_index) {
                    continue;
                }
                // Repeated-stop geometry must match the same downstream route
                // occurrence, not merely the same physical stop.
                if (continuation.to_index != desired_route_to_index) {
                    continue;
                }
                return continuation.arrival;
            }

            return std::nullopt;
        }

        std::optional<Time> same_trip_continuation_arrival_from_label(
              const PreprocessedNetwork&       network
            , const DayLevelTimedSupportLabel& label
            , const ConnectionSegment&         successor
            , const RouteSegment&              successor_route_segment
        ) noexcept {
            if (
                   !label.trip.has_value()
                || !label.to_index.has_value()
                || !successor.to_index.has_value()
            ) {
                return std::nullopt;
            }

            const auto& current_route_segment = route_segment_at(network, label.route_segment);
            const auto* current_line          = line_topology_of(current_route_segment);
            const auto* successor_line        = line_topology_of(successor_route_segment);
            if (!current_line || !successor_line) {
                return std::nullopt;
            }
            if (current_line->line != successor_line->line
                || current_line->route != successor_line->route) {
                return std::nullopt;
            }

            const auto current_stop           = occurrence_key(current_line->to);
            const auto desired_route_to_index = successor.to_index.value();
            const auto range                  = timed_bucket_range(network.connection_index, current_stop);
            if (!range) {
                return std::nullopt;
            }

            const auto [start, end] = *range;
            for (std::size_t i = start; i < end; ++i) {
                const auto  connection_id              = network.connection_index.timed_order[i];
                const auto& continuation               = connection_segment_at(network, connection_id);
                const auto& continuation_route_segment = route_segment_at(
                      network
                    , continuation.route_segment
                );
                const auto* continuation_line = line_topology_of(continuation_route_segment);
                if (!continuation_line) {
                    continue;
                }
                if (continuation_line->line != current_line->line
                    || continuation_line->route != current_line->route) {
                    continue;
                }
                if (continuation.trip != label.trip) {
                    continue;
                }
                if (continuation.from_index != label.to_index) {
                    continue;
                }
                if (continuation.to_index != desired_route_to_index) {
                    continue;
                }
                return continuation.arrival;
            }

            return std::nullopt;
        }

        bool improves_repeated_stop_reboarding(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , const RouteSegment&        successor_route_segment
        ) noexcept {
            if (!is_repeated_stop_reboarding_case(branch, successor, successor_route_segment)) {
                return true;
            }
            const auto continuation_arrival = same_trip_continuation_arrival(
                  branch
                , network
                , successor
                , successor_route_segment
            );
            if (!continuation_arrival.has_value() || !successor.arrival.has_value()) {
                return false;
            }
            return successor.arrival->value() < continuation_arrival->value();
        }

        bool improves_repeated_stop_reboarding_from_label(
              const PreprocessedNetwork&       network
            , const SearchBranch&              branch
            , const DayLevelTimedSupportLabel& label
            , const ConnectionSegment&         successor
            , const RouteSegment&              successor_route_segment
        ) noexcept {
            if (!branch.metrics.departure.has_value()) {
                return true;
            }
            if (!is_timed_connection(successor)) {
                return true;
            }

            const auto& current_route_segment = route_segment_at(network, label.route_segment);
            const auto* current_line          = line_topology_of(current_route_segment);
            const auto* successor_line        = line_topology_of(successor_route_segment);
            if (!current_line || !successor_line) {
                return true;
            }
            if (current_line->line != successor_line->line
                || current_line->route != successor_line->route) {
                return true;
            }
            if (!label.trip.has_value() || !successor.trip.has_value()
                || label.trip == successor.trip) {
                return true;
            }
            if (current_line->to.stop != successor_line->from.stop) {
                return true;
            }
            if (!label.to_index.has_value() || !successor.from_index.has_value()) {
                return true;
            }
            if (!(label.to_index.value() < successor.from_index.value())) {
                return true;
            }

            const auto continuation_arrival = same_trip_continuation_arrival_from_label(
                  network
                , label
                , successor
                , successor_route_segment
            );
            if (!continuation_arrival.has_value() || !successor.arrival.has_value()) {
                return false;
            }
            return successor.arrival->value() < continuation_arrival->value();
        }

        [[nodiscard]] BranchState day_level_support_branch_state(
            const SearchBranch& branch
        ) noexcept {
            return BranchState{
                  .current_arrival_time = branch.metrics.current_time
                , .last_segment         = branch.trace.last_timed_segment
                , .last_route_segment   = branch.trace.last_timed_route_segment
                , .transfer_count       = branch.metrics.departure.has_value()
                    ? std::optional<TransferCount>{ branch.metrics.transfers }
                    : std::nullopt
            };
        }

        [[nodiscard]] std::optional<Time> support_label_current_time(
              const SearchBranch&          branch
            , const DayLevelTimedSupportLabel& label
        ) noexcept {
            if (!branch.metrics.current_time.has_value()) {
                return std::nullopt;
            }
            if (branch.trace.last_timed_segment == nullptr
                || !branch.trace.last_timed_segment->arrival.has_value()) {
                return label.arrival;
            }

            return Time{
                label.arrival.value()
                + branch.metrics.current_time->value()
                - branch.trace.last_timed_segment->arrival->value()
            };
        }

        [[nodiscard]] BranchState day_level_support_label_branch_state(
              const PreprocessedNetwork&       network
            , const SearchBranch&              branch
            , const DayLevelTimedSupportLabel& label
        ) noexcept {
            return BranchState{
                  .current_arrival_time = support_label_current_time(branch, label)
                , .last_segment         = &connection_segment_at(network, label.connection)
                , .last_route_segment   = &route_segment_at(network, label.route_segment)
                , .transfer_count       = branch.metrics.departure.has_value()
                    ? std::optional<TransferCount>{ branch.metrics.transfers }
                    : std::nullopt
            };
        }

        [[nodiscard]] std::optional<DayLevelTimedSupportLabel> make_day_level_timed_support_label(
              const ConnectionSegment& connection
        ) noexcept {
            if (!connection.departure.has_value() || !connection.arrival.has_value()) {
                return std::nullopt;
            }

            return DayLevelTimedSupportLabel{
                  .connection    = connection.id
                , .route_segment = connection.route_segment
                , .trip          = connection.trip
                , .from_index    = connection.from_index
                , .to_index      = connection.to_index
                , .departure     = *connection.departure
                , .arrival       = *connection.arrival
            };
        }

        [[nodiscard]] std::optional<DayLevelTimedSupportLabel> feasible_day_level_timed_support_label(
              const PreprocessedNetwork&  network
            , const SearchBranch&         branch
            , ConnectionSegmentId         connection_id
            , const TransferLimits&       limits
            , const SearchTimeDomain*     first_departure_domain
        ) {
            const auto state = day_level_support_branch_state(branch);
            const auto& connection = connection_segment_at(network, connection_id);
            const auto& route_segment = route_segment_at(network, connection.route_segment);
            const auto label = make_day_level_timed_support_label(connection);
            if (!label.has_value()) {
                return std::nullopt;
            }
            if (!first_timed_departure_allowed(
                      branch
                    , connection
                    , first_departure_domain
                    , limits
            )) {
                return std::nullopt;
            }

            if (!branch.od_day_carrier.support_envelope.labels.empty()
                && branch.metrics.departure.has_value()) {
                const auto feasible_from_envelope = std::any_of(
                      branch.od_day_carrier.support_envelope.labels.begin()
                    , branch.od_day_carrier.support_envelope.labels.end()
                    , [&](const DayLevelTimedSupportLabel& support_label) {
                          return is_branch_extension_feasible(
                                day_level_support_label_branch_state(
                                      network
                                    , branch
                                    , support_label
                                )
                              , connection
                              , route_segment
                              , limits
                          )
                          && improves_repeated_stop_reboarding_from_label(
                                network
                              , branch
                              , support_label
                              , connection
                              , route_segment
                          );
                      }
                );
                if (!feasible_from_envelope) {
                    return std::nullopt;
                }
            } else {
                if (!is_branch_extension_feasible(
                          state
                        , connection
                        , route_segment
                        , limits
                )) {
                    return std::nullopt;
                }
                if (!improves_repeated_stop_reboarding(
                          branch
                        , network
                        , connection
                        , route_segment
                )) {
                    return std::nullopt;
                }
            }
            return label;
        }

        struct DayLevelTimedSupportPropagation final {
            DayLevelTimedSupportLabel label;
            TimedSupportEnvelope      envelope{};
        };

        [[nodiscard]] std::optional<DayLevelTimedSupportPropagation> propagate_day_level_timed_support(
              const PreprocessedNetwork& network
            , const SearchBranch&        branch
            , const DayLevelRideSupport& support
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
        ) {
            TimedSupportLabelVector feasible_labels;
            feasible_labels.reserve(
                std::min(
                      support.support_labels.size()
                    , kMaxTimedSupportEnvelopeLabels
                )
            );
            std::optional<TimedSupportLabel> representative;
            std::optional<RouteSegmentId> representative_route_segment;
            for (const auto connection_id : support.support_labels) {
                const auto label = feasible_day_level_timed_support_label(
                      network
                    , branch
                    , connection_id
                    , limits
                    , first_departure_domain
                );
                if (label.has_value()) {
                    if (!representative.has_value()
                        || better_timed_support_label(*label, *representative)) {
                        representative = *label;
                        representative_route_segment = label->route_segment;
                    }
                    retain_timed_support_label(feasible_labels, *label);
                }
            }
            if (!representative.has_value() || !representative_route_segment.has_value()) {
                return std::nullopt;
            }
            return DayLevelTimedSupportPropagation{
                  .label = *representative
                , .envelope = make_timed_support_envelope(
                      route_segment_at(network, *representative_route_segment)
                    , std::move(feasible_labels)
                  )
            };
        }

        template <typename Visitor, typename RejectedWalkVisitor>
        void for_each_day_level_supply_successor(
              const DayLevelSupplySearchGraph& day_graph
            , const PreprocessedNetwork&        network
            , ZoneId                            origin
            , const ActiveDestinationMembership& active_destinations
            , const SearchBranch&               branch
            , const TransferLimits&             limits
            , const SearchTimeDomain*           first_departure_domain
            , Visitor&&                         visit
            , RejectedWalkVisitor&&             reject_walk
        ) {
            auto&& visitor = visit;
            auto&& walk_rejection_visitor = reject_walk;

            auto visit_walk_bucket = [&](const auto& buckets) {
                const auto found = buckets.find(branch.trace.current_physical);
                if (found != buckets.end()) {
                    for (const auto& support : found->second) {
                        for (const auto connection_id : support.support_labels) {
                            const auto& connection = connection_segment_at(network, connection_id);
                            const auto& route_segment = route_segment_at(network, connection.route_segment);
                            if (auto transition = admissible_walk_extension_transition(
                                  origin
                                , active_destinations
                                , branch
                                , route_segment
                            )) {
                                visitor(
                                    SearchSuccessor{
                                          .connection      = connection_id
                                        , .walk_transition = *transition
                                        , .day_level_edge  = support.edge
                                    }
                                );
                                break;
                            }
                        }
                    }
                }
            };

            switch (branch.trace.phase) {
                case SearchBranchPhase::AtOrigin:
                    visit_walk_bucket(day_graph.access_walks_by_from);
                    break;

                case SearchBranchPhase::AfterTimedRide:
                    visit_walk_bucket(day_graph.egress_walks_by_from);
                    visit_walk_bucket(day_graph.transfer_walks_by_from);
                    break;

                case SearchBranchPhase::BeforeFirstBoarding:
                case SearchBranchPhase::AfterTransferWalk: {
                    const auto found = day_graph.transfer_walks_by_from.find(
                        branch.trace.current_physical
                    );
                    walk_rejection_visitor(
                        found == day_graph.transfer_walks_by_from.end()
                            ? 0u
                            : found->second.size()
                    );
                    break;
                }

                case SearchBranchPhase::Completed:
                    break;
            }

            const auto ride_bucket = day_graph.rides_by_from.find(
                branch.trace.current_physical
            );
            if (ride_bucket == day_graph.rides_by_from.end()) {
                return;
            }

            for (const auto& support : ride_bucket->second) {
                const auto propagation = propagate_day_level_timed_support(
                      network
                    , branch
                    , support
                    , limits
                    , first_departure_domain
                );
                if (propagation.has_value()) {
                    visitor(
                        SearchSuccessor{
                              .connection       = propagation->label.connection
                            , .day_level_edge   = support.edge
                            , .support_envelope = propagation->envelope
                        }
                    );
                }
            }
        }

        [[nodiscard]] Time add_time(
              Time lhs
            , Time rhs
        ) noexcept {
            return Time{ lhs.value() + rhs.value() };
        }

        [[nodiscard]] ConnectionLeg make_walk_leg(
              ConnectionLegKind   kind
            , ConnectionSegmentId segment_id
            , const RouteSegment& route_segment
            , Time                start_time
        ) {
            return ConnectionLeg{
                  .kind               = kind
                , .connection_segment = segment_id
                , .route_segment      = route_segment.id
                , .physical_from      = physical_from_key(route_segment)
                , .physical_to        = physical_to_key(route_segment)
                , .occurrence_from    = std::nullopt
                , .occurrence_to      = std::nullopt
                , .line               = std::nullopt
                , .route              = std::nullopt
                , .trip               = std::nullopt
                , .start_time         = start_time
                , .end_time           = add_time(start_time, route_segment.run_time)
                , .length             = route_segment.length
                , .fare               = 0.0
            };
        }

        [[nodiscard]] std::optional<ConnectionLeg> make_transfer_wait_leg(
              Time                        current_time
            , const ConnectionSegment&    segment
            , EndpointKey                  physical
        ) noexcept {
            if (!segment.departure.has_value()) {
                return std::nullopt;
            }
            if (segment.departure->value() <= current_time.value()) {
                return std::nullopt;
            }
            return ConnectionLeg{
                  .kind               = ConnectionLegKind::TransferWait
                , .connection_segment = std::nullopt
                , .route_segment      = std::nullopt
                , .physical_from      = physical
                , .physical_to        = physical
                , .occurrence_from    = std::nullopt
                , .occurrence_to      = std::nullopt
                , .line               = std::nullopt
                , .route              = std::nullopt
                , .trip               = std::nullopt
                , .start_time         = current_time
                , .end_time           = *segment.departure
                , .length             = Length{ 0.0 }
                , .fare               = 0.0
            };
        }

        [[nodiscard]] std::optional<ConnectionLeg> make_transfer_wait_leg(
              const SearchPartialMetrics& metrics
            , const ConnectionSegment&    segment
            , EndpointKey                  physical
        ) noexcept {
            if (!metrics.current_time.has_value()) {
                return std::nullopt;
            }
            return make_transfer_wait_leg(*metrics.current_time, segment, physical);
        }

        [[nodiscard]] ConnectionLeg make_ride_leg(
              const ConnectionSegment& segment
            , const RouteSegment&      route_segment
        ) {
            const auto* line = line_topology_of(route_segment);
            return ConnectionLeg{
                  .kind               = ConnectionLegKind::Ride
                , .connection_segment = segment.id
                , .route_segment      = route_segment.id
                , .physical_from      = physical_from_key(route_segment)
                , .physical_to        = physical_to_key(route_segment)
                , .occurrence_from    = occurrence_key(line->from)
                , .occurrence_to      = occurrence_key(line->to)
                , .line               = line->line
                , .route              = line->route
                , .trip               = segment.trip
                , .start_time         = *segment.departure
                , .end_time           = *segment.arrival
                , .length             = route_segment.length
                , .fare               = segment.fare.value_or(0.0)
            };
        }

        [[nodiscard]] DayPathLeg make_day_path_walk_leg(
              ConnectionLegKind   kind
            , const RouteSegment& route_segment
        ) noexcept {
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

        [[nodiscard]] DayPathLeg make_day_path_ride_leg(
            const RouteSegment& route_segment
        ) noexcept {
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

        [[nodiscard]] SearchPartialTrace extend_trace_with_walk(
              SearchPartialTrace            trace
            , std::size_t                    branch_index
            , const RouteSegment&            route_segment
            , ConnectionSegmentId            segment_id
            , std::optional<DayLevelSupplyEdgeRef> day_level_edge
            , const WalkExtensionTransition& transition
        ) {
            const auto next_physical = physical_to_key(route_segment);
            trace.parent_branch      = branch_index;
            trace.incoming_segment   = segment_id;
            trace.incoming_day_level_edge = day_level_edge;
            trace.current_physical   = next_physical;
            trace.current_occurrence = std::nullopt;
            trace.phase              = transition.next_phase;
            return trace;
        }

        [[nodiscard]] SearchPartialMetrics extend_metrics_with_walk(
              SearchPartialMetrics metrics
            , const RouteSegment&   route_segment
            , ConnectionLegKind     kind
        ) {
            switch (kind) {
                case ConnectionLegKind::AccessWalk:
                    metrics.access_time = Time{
                        metrics.access_time.value() + route_segment.run_time.value()
                    };
                    return metrics;

                case ConnectionLegKind::TransferWalk:
                    metrics.current_time = Time{
                        metrics.current_time->value() + route_segment.run_time.value()
                    };
                    metrics.transfer_walk_time = Time{
                        metrics.transfer_walk_time.value() + route_segment.run_time.value()
                    };
                    return metrics;

                case ConnectionLegKind::EgressWalk:
                    metrics.current_time = Time{
                        metrics.current_time->value() + route_segment.run_time.value()
                    };
                    metrics.egress_time = Time{
                        metrics.egress_time.value() + route_segment.run_time.value()
                    };
                    return metrics;

                case ConnectionLegKind::Ride:
                case ConnectionLegKind::InitialWait:
                case ConnectionLegKind::TransferWait:
                case ConnectionLegKind::FinalWait:
                    return metrics;
            }

            return metrics;
        }

        [[nodiscard]] SearchBranch extend_with_walk(
              const SearchBranch&           branch
            , std::size_t                   branch_index
            , const RouteSegment&           route_segment
            , ConnectionSegmentId           segment_id
            , std::optional<DayLevelSupplyEdgeRef> day_level_edge
            , const WalkExtensionTransition& transition
        ) {
            const auto trace = extend_trace_with_walk(
                  branch.trace
                , branch_index
                , route_segment
                , segment_id
                , day_level_edge
                , transition
            );
            return SearchBranch{
                  .trace           = trace
                , .metrics         = extend_metrics_with_walk(
                      branch.metrics
                    , route_segment
                    , transition.kind
                  )
                , .od_day_carrier  = OdDayProductionCarrier{
                      .path_identity = append_od_day_path_leg(
                            branch.od_day_carrier.path_identity
                          , make_day_path_walk_leg(transition.kind, route_segment)
                      )
                    , .support_envelope = branch.od_day_carrier.support_envelope
                    , .support_prefix   = append_od_day_support_segment(
                          branch.od_day_carrier.support_prefix
                        , segment_id
                      )
                  }
            };
        }

        [[nodiscard]] SearchPartialTrace extend_trace_with_timed(
              SearchPartialTrace       trace
            , std::size_t               branch_index
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
            , SearchBranchPhase         next_phase
            , std::optional<DayLevelSupplyEdgeRef> day_level_edge
        ) {
            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            trace.parent_branch                  = branch_index;
            trace.incoming_segment               = segment.id;
            trace.incoming_day_level_edge        = day_level_edge;
            trace.last_day_level_ride_edge       = day_level_edge;
            trace.current_physical               = physical_to_key(route_segment);
            trace.current_occurrence             = next_occurrence;
            trace.phase                          = next_phase;
            trace.last_timed_segment             = &segment;
            trace.last_timed_route_segment       = &route_segment;
            return trace;
        }

        std::optional<SearchBranchPhase> timed_extension_transition(
            SearchBranchPhase phase
        ) noexcept {
            switch (phase) {
                case SearchBranchPhase::BeforeFirstBoarding:
                case SearchBranchPhase::AfterTimedRide:
                case SearchBranchPhase::AfterTransferWalk:
                    return SearchBranchPhase::AfterTimedRide;

                case SearchBranchPhase::AtOrigin:
                case SearchBranchPhase::Completed:
                    return std::nullopt;
            }

            return std::nullopt;
        }

        [[nodiscard]] CapacityExposure add_capacity_exposure(
              CapacityExposure lhs
            , CapacityExposure rhs
        ) noexcept {
            return CapacityExposure{
                Time{
                    lhs.equivalent_time.value()
                    + rhs.equivalent_time.value()
                }
            };
        }

        [[nodiscard]] mathfp::Expected<CapacityExposure> timed_successor_capacity_exposure(
              const ConnectionSegment& segment
            , const RouteSegment&      route_segment
            , std::optional<IntervalId> interval
            , const SearchCostContext& search_cost
        ) {
            switch (search_cost.mode) {
                case SearchCostMode::BaseOnly:
                    return CapacityExposure{ Time{ 0.0 } };

                case SearchCostMode::CapacityAware:
                    if (!interval.has_value()) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("capacity-aware origin-period search requires task-indexed interval exposure")
                        );
                    }
                    return search_capacity_exposure(
                          make_ride_leg(segment, route_segment)
                        , *interval
                        , search_cost.capacity
                    );
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("unknown search cost mode")
                    .ctx("mode", static_cast<std::int64_t>(search_cost.mode))
            );
        }

        [[nodiscard]] mathfp::Expected<SearchPartialMetrics> extend_metrics_with_timed(
              SearchPartialMetrics      metrics
            , const ConnectionSegment& segment
            , CapacityExposure          capacity_exposure
        ) {
            const auto had_departure = metrics.departure.has_value();
            if (!had_departure) {
                metrics.departure = Time{
                    segment.departure->value() - metrics.access_time.value()
                };
            } else {
                metrics.transfer_wait_time = Time{
                    metrics.transfer_wait_time.value()
                    + (segment.departure->value() - metrics.current_time->value())
                };
                MATHFP_TRY_LET(
                      TransferCount
                    , next_transfers
                    , next_transfer_count(metrics.transfers)
                );
                metrics.transfers = next_transfers;
            }

            metrics.in_vehicle_time = Time{
                metrics.in_vehicle_time.value()
                + (segment.arrival->value() - segment.departure->value())
            };
            metrics.current_time = *segment.arrival;
            metrics.fare         = metrics.fare + segment.fare.value_or(0.0);
            metrics.capacity_exposure = add_capacity_exposure(
                  metrics.capacity_exposure
                , capacity_exposure
            );
            return metrics;
        }

        [[nodiscard]] mathfp::Expected<SearchBranch> extend_with_timed(
              const SearchBranch&      branch
            , std::size_t              branch_index
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
            , SearchBranchPhase         next_phase
            , std::optional<DayLevelSupplyEdgeRef> day_level_edge
            , TimedSupportEnvelope      support_envelope
            , std::optional<IntervalId> interval
            , const SearchCostContext& search_cost
        ) {
            MATHFP_TRY_LET(
                  CapacityExposure
                , capacity_exposure
                , timed_successor_capacity_exposure(
                      segment
                    , route_segment
                    , interval
                    , search_cost
                )
            );
            MATHFP_TRY_LET(
                  SearchPartialMetrics
                , extended_metrics
                , extend_metrics_with_timed(
                      branch.metrics
                    , segment
                    , capacity_exposure
                )
            );
            return SearchBranch{
                  .trace           = extend_trace_with_timed(
                        branch.trace
                      , branch_index
                      , segment
                      , route_segment
                      , next_phase
                      , day_level_edge
                  )
                , .metrics         = extended_metrics
                , .od_day_carrier  = OdDayProductionCarrier{
                      .path_identity = append_od_day_path_leg(
                            branch.od_day_carrier.path_identity
                          , make_day_path_ride_leg(route_segment)
                      )
                    , .support_envelope = std::move(support_envelope)
                    , .support_prefix   = append_od_day_support_segment(
                          branch.od_day_carrier.support_prefix
                        , segment.id
                      )
                  }
            };
        }

        [[nodiscard]] const char* search_branch_phase_name(
            SearchBranchPhase phase
        ) noexcept {
            switch (phase) {
                case SearchBranchPhase::AtOrigin:
                    return "at_origin";

                case SearchBranchPhase::BeforeFirstBoarding:
                    return "before_first_boarding";

                case SearchBranchPhase::AfterTimedRide:
                    return "after_timed_ride";

                case SearchBranchPhase::AfterTransferWalk:
                    return "after_transfer_walk";

                case SearchBranchPhase::Completed:
                    return "completed";
            }

            return "unknown";
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> phase_invariant_error(
              const SearchBranch& branch
            , const char*         reason
        ) {
            return mathfp::unexpected(
                mathfp::internal_error("search branch phase invariant violation")
                    .ctx("reason", std::string(reason))
                    .ctx("phase", std::string(search_branch_phase_name(branch.trace.phase)))
                    .ctx("origin", branch.trace.origin.get())
                    .ctx("current_kind", static_cast<std::int64_t>(branch.trace.current_physical.kind))
                    .ctx("current_id", branch.trace.current_physical.id)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_branch_phase_invariants(
            const SearchBranch& branch
        ) {
            const auto physical = branch.trace.current_physical;
            switch (branch.trace.phase) {
                case SearchBranchPhase::AtOrigin:
                    if (physical != endpoint_key(branch.trace.origin)) {
                        return phase_invariant_error(branch, "at-origin branch is not located at its origin zone");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "at-origin branch has stop occurrence");
                    }
                    if (branch.trace.parent_branch.has_value()
                        || branch.trace.incoming_segment.has_value()) {
                        return phase_invariant_error(branch, "at-origin branch has predecessor edge");
                    }
                    if (branch.trace.last_timed_segment != nullptr
                        || branch.trace.last_timed_route_segment != nullptr) {
                        return phase_invariant_error(branch, "at-origin branch has timed context");
                    }
                    if (branch.metrics.departure.has_value()
                        || branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "at-origin branch has time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::BeforeFirstBoarding:
                    if (physical.kind != EndpointKind::Stop) {
                        return phase_invariant_error(branch, "preboarding branch is not located at a stop");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "preboarding branch has stop occurrence");
                    }
                    if (!branch.trace.parent_branch.has_value()
                        || !branch.trace.incoming_segment.has_value()) {
                        return phase_invariant_error(branch, "preboarding branch has no access-walk predecessor");
                    }
                    if (branch.trace.last_timed_segment != nullptr
                        || branch.trace.last_timed_route_segment != nullptr) {
                        return phase_invariant_error(branch, "preboarding branch has timed context");
                    }
                    if (branch.metrics.departure.has_value()
                        || branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "preboarding branch has time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::AfterTimedRide:
                    if (physical.kind != EndpointKind::Stop) {
                        return phase_invariant_error(branch, "after-timed branch is not located at a stop");
                    }
                    if (!branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "after-timed branch has no stop occurrence");
                    }
                    if (branch.trace.last_timed_segment == nullptr
                        || branch.trace.last_timed_route_segment == nullptr) {
                        return phase_invariant_error(branch, "after-timed branch has no timed context");
                    }
                    if (!branch.metrics.departure.has_value()
                        || !branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "after-timed branch has incomplete time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::AfterTransferWalk:
                    if (physical.kind != EndpointKind::Stop) {
                        return phase_invariant_error(branch, "after-transfer-walk branch is not located at a stop");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "after-transfer-walk branch has stop occurrence");
                    }
                    if (branch.trace.last_timed_segment == nullptr
                        || branch.trace.last_timed_route_segment == nullptr) {
                        return phase_invariant_error(branch, "after-transfer-walk branch has no timed context");
                    }
                    if (!branch.metrics.departure.has_value()
                        || !branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "after-transfer-walk branch has incomplete time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::Completed:
                    if (physical.kind != EndpointKind::Zone) {
                        return phase_invariant_error(branch, "completed branch is not located at a zone");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "completed branch has stop occurrence");
                    }
                    if (branch.trace.last_timed_segment == nullptr
                        || branch.trace.last_timed_route_segment == nullptr) {
                        return phase_invariant_error(branch, "completed branch has no timed context");
                    }
                    if (!branch.metrics.departure.has_value()
                        || !branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "completed branch has incomplete time state");
                    }
                    return mathfp::kUnit;
            }

            return phase_invariant_error(branch, "unknown branch phase");
        }

        mathfp::Expected<std::optional<SearchBranch>> extend_branch(
              const BranchArena&         branches
            , std::size_t                branch_index
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const SearchSuccessor&     successor_ref
            , std::optional<IntervalId>   interval
            , const SearchCostContext&    search_cost
        ) {
            const auto& successor = connection_segment_at(network, successor_ref.connection);
            const auto& route_segment = route_segment_at(network, successor.route_segment);
            if (is_walk_connection(successor)) {
                if (!successor_ref.walk_transition.has_value()) {
                    return std::optional<SearchBranch>{};
                }
                const auto next_physical = physical_to_key(route_segment);
                if (branch_revisits_physical(branches, branch, network, next_physical)) {
                    return std::optional<SearchBranch>{};
                }
                return std::optional<SearchBranch>{
                    extend_with_walk(
                          branch
                        , branch_index
                        , route_segment
                        , successor.id
                        , successor_ref.day_level_edge
                        , *successor_ref.walk_transition
                    )
                };
            }

            const auto next_phase = timed_extension_transition(branch.trace.phase);
            if (!next_phase.has_value()) {
                return std::optional<SearchBranch>{};
            }
            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            if (branch_revisits_occurrence(branches, branch, network, next_occurrence)) {
                return std::optional<SearchBranch>{};
            }
            MATHFP_TRY_LET(
                  SearchBranch
                , extended
                , extend_with_timed(
                      branch
                    , branch_index
                    , successor
                    , route_segment
                    , *next_phase
                    , successor_ref.day_level_edge
                    , successor_ref.support_envelope.value_or(
                          propagate_timed_support_envelope(successor, route_segment)
                      )
                    , interval
                    , search_cost
                )
            );
            return std::optional<SearchBranch>{ std::move(extended) };
        }

        mathfp::Expected<PaperConnectionPrefixEvaluation>
        evaluate_paper_connection_prefix_before_branch(
              const BranchArena&         branches
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const SearchSuccessor&     successor_ref
            , std::optional<IntervalId>  interval
            , const SearchCostContext&   search_cost
            , bool&                      rejected_cycle
        ) {
            rejected_cycle = false;
            const auto& successor = connection_segment_at(network, successor_ref.connection);
            const auto& route_segment = route_segment_at(network, successor.route_segment);

            auto metrics = branch.metrics;
            auto next_physical = physical_to_key(route_segment);

            if (is_walk_connection(successor)) {
                if (!successor_ref.walk_transition.has_value()) {
                    return PaperConnectionPrefixEvaluation{};
                }
                if (branch_revisits_physical(branches, branch, network, next_physical)) {
                    rejected_cycle = true;
                    return PaperConnectionPrefixEvaluation{};
                }
                metrics = extend_metrics_with_walk(
                      std::move(metrics)
                    , route_segment
                    , successor_ref.walk_transition->kind
                );
            } else {
                if (!timed_extension_transition(branch.trace.phase).has_value()) {
                    return PaperConnectionPrefixEvaluation{};
                }
                const auto next_occurrence = occurrence_key(
                    line_topology_of(route_segment)->to
                );
                if (branch_revisits_occurrence(branches, branch, network, next_occurrence)) {
                    rejected_cycle = true;
                    return PaperConnectionPrefixEvaluation{};
                }
                MATHFP_TRY_LET(
                      CapacityExposure
                    , capacity_exposure
                    , timed_successor_capacity_exposure(
                          successor
                        , route_segment
                        , interval
                        , search_cost
                    )
                );
                MATHFP_TRY_ASSIGN(
                    metrics,
                    extend_metrics_with_timed(
                      std::move(metrics)
                    , successor
                    , capacity_exposure
                    )
                );
            }

            MATHFP_TRY_LET(
                  PartialPruningMetrics
                , pruning_metrics
                , make_day_path_pruning_metrics(metrics, search_cost)
            );
            return PaperConnectionPrefixEvaluation{
                  .kind = PaperConnectionPrefixKind::ConnectionPrefix
                , .connection_candidate = PaperConnectionCandidateMetrics{
                      .node = PaperConnectionNodeKey{ .physical = next_physical }
                    , .metrics = std::move(pruning_metrics)
                }
            };
        }

        struct PaperSuccessorGenerator final {
            //tex:
            // Paper successor contract:
            // $$c_x^*\mapsto\{s^*_{x,y}\mid s^*_{x,y}\ \text{is insertable before }C_y\}.$$
            // The generator applies branch phase, temporal lookup windows,
            // same-trip/same-line rules and the hard transfer bound before
            // exposing a candidate to $$C_y$$. The later feasibility predicate is
            // retained only as a defensive invariant.
            const PreprocessedNetwork&            network;
            ZoneId                                origin;
            const ActiveDestinationMembership&    active_destinations;
            const SearchBranch&                   branch;
            const TransferLimits&                 limits;
            const SearchTimeDomain*               first_departure_domain{};
            TaskSearchStats*                      stats{};

            enum class InsertabilityRejection : std::uint8_t {
                  None
                , FirstDepartureDomain
                , SameTrip
                , SameLine
                , Feasibility
            };

            [[nodiscard]] bool timed_lookup_allowed() const {
                if (!timed_extension_transition(branch.trace.phase).has_value()) {
                    if (stats != nullptr) {
                        ++stats->paper_timed_lookup_skipped_phase;
                    }
                    return false;
                }
                if (!branch.metrics.departure.has_value()) {
                    return true;
                }
                if (branch.metrics.transfers >= limits.max_transfers) {
                    if (stats != nullptr) {
                        ++stats->paper_timed_lookup_skipped_transfer_budget;
                    }
                    return false;
                }
                return true;
            }

            [[nodiscard]] InsertabilityRejection insertability_rejection(
                  ConnectionSegmentId connection_id
                , std::optional<Time> current_arrival_time
            ) const {
                const auto& successor = connection_segment_at(network, connection_id);
                const auto& route_segment = route_segment_at(
                      network
                    , successor.route_segment
                );
                if (!first_timed_departure_allowed(
                      branch
                    , successor
                    , first_departure_domain
                    , limits
                )) {
                    return InsertabilityRejection::FirstDepartureDomain;
                }
                if (branch.trace.last_timed_segment != nullptr
                    && transfer_reuses_same_trip(
                          *branch.trace.last_timed_segment
                        , successor
                    )) {
                    return InsertabilityRejection::SameTrip;
                }
                if (is_same_line_transfer_candidate(
                      branch
                    , successor
                    , route_segment
                )) {
                    const auto allowed_loop_reboarding =
                           is_repeated_stop_reboarding_case(branch, successor, route_segment)
                        && improves_repeated_stop_reboarding(
                              branch
                            , network
                            , successor
                            , route_segment
                        );
                    if (!allowed_loop_reboarding) {
                        return InsertabilityRejection::SameLine;
                    }
                }
                const BranchState state{
                      .current_arrival_time = current_arrival_time
                    , .last_segment         = branch.trace.last_timed_segment
                    , .last_route_segment   = branch.trace.last_timed_route_segment
                    , .transfer_count       = branch.metrics.departure.has_value()
                        ? std::optional<TransferCount>{ branch.metrics.transfers }
                        : std::nullopt
                };
                if (!is_branch_extension_feasible(
                      state
                    , successor
                    , route_segment
                    , limits
                )) {
                    return InsertabilityRejection::Feasibility;
                }
                return InsertabilityRejection::None;
            }

            [[nodiscard]] bool is_insertable_paper_successor(
                  ConnectionSegmentId connection_id
                , std::optional<Time> current_arrival_time
            ) const {
                return insertability_rejection(connection_id, current_arrival_time)
                    == InsertabilityRejection::None;
            }

            [[nodiscard]] bool timed_successor_insertable_before_visitor(
                  ConnectionSegmentId connection_id
                , std::optional<Time> current_arrival_time
            ) const {
                const auto rejection =
                    insertability_rejection(connection_id, current_arrival_time);
                switch (rejection) {
                    case InsertabilityRejection::None:
                        return true;

                    case InsertabilityRejection::FirstDepartureDomain:
                        if (stats != nullptr) {
                            ++stats->paper_timed_successor_rejected_time_domain;
                        }
                        return false;

                    case InsertabilityRejection::SameTrip:
                        if (stats != nullptr) {
                            ++stats->paper_timed_successor_rejected_same_trip;
                        }
                        return false;

                    case InsertabilityRejection::SameLine:
                        if (stats != nullptr) {
                            ++stats->paper_timed_successor_rejected_same_line;
                        }
                        return false;

                    case InsertabilityRejection::Feasibility:
                        if (stats != nullptr) {
                            ++stats->paper_timed_successor_rejected_feasibility;
                        }
                        return false;
                }
                return true;
            }

            template <typename Visitor>
            void for_each_walk_successor(
                  std::span<const ConnectionSegmentId> connections
                , Visitor&&                            visit
            ) const {
                auto&& visitor = visit;
                for (const auto connection_id : connections) {
                    const auto& connection = connection_segment_at(network, connection_id);
                    const auto& route_segment = route_segment_at(
                          network
                        , connection.route_segment
                    );
                    if (auto transition = admissible_walk_extension_transition(
                          origin
                        , active_destinations
                        , branch
                        , route_segment
                    )) {
                        visitor(
                            SearchSuccessor{
                                  .connection      = connection_id
                                , .walk_transition = *transition
                            }
                        );
                    }
                }
            }

            template <typename Visitor>
            void for_each_timed_ride_successor(
                Visitor&& visit
            ) const {
                if (!timed_lookup_allowed()) {
                    return;
                }

                auto&& visitor = visit;
                auto visit_timed = [&](ConnectionSegmentId connection_id) {
                    if (!timed_successor_insertable_before_visitor(
                          connection_id
                        , branch.metrics.current_time
                    )) {
                        return;
                    }
                    const auto& connection = connection_segment_at(network, connection_id);
                    const auto& route_segment = route_segment_at(
                          network
                        , connection.route_segment
                    );
                    visitor(
                        SearchSuccessor{
                              .connection = connection_id
                            , .support_envelope =
                                propagate_timed_support_envelope(
                                      connection
                                    , route_segment
                                )
                        }
                    );
                };
                for_each_timed_successor(
                      network
                    , branch.trace.current_physical
                    , branch.metrics.current_time
                    , limits
                    , first_departure_domain
                    , visit_timed
                );
            }

            template <typename Visitor, typename RejectedWalkVisitor>
            void for_each(
                  Visitor&&             visit
                , RejectedWalkVisitor&& reject_walk
            ) const {
                auto&& visitor = visit;
                auto&& walk_rejection_visitor = reject_walk;

                switch (branch.trace.phase) {
                    case SearchBranchPhase::AtOrigin:
                        if (stats != nullptr) {
                            ++stats->walk_lookup.access;
                        }
                        for_each_walk_successor(
                            access_walk_connections_from(
                                  network.connection_index
                                , branch.trace.current_physical
                            )
                          , visitor
                        );
                        break;

                    case SearchBranchPhase::AfterTimedRide:
                        if (stats != nullptr) {
                            ++stats->walk_lookup.egress;
                        }
                        for_each_walk_successor(
                            egress_walk_connections_from(
                                  network.connection_index
                                , branch.trace.current_physical
                            )
                          , visitor
                        );
                        if (can_start_transfer_walk(branch, limits)) {
                            if (stats != nullptr) {
                                ++stats->walk_lookup.transfer;
                            }
                            for_each_walk_successor(
                                transfer_walk_connections_from(
                                      network.connection_index
                                    , branch.trace.current_physical
                                )
                              , visitor
                            );
                        } else if (stats != nullptr) {
                            ++stats->walk_lookup.skipped_by_transfer_budget;
                        }
                        break;

                    case SearchBranchPhase::BeforeFirstBoarding:
                    case SearchBranchPhase::AfterTransferWalk:
                        if (stats != nullptr) {
                            ++stats->walk_lookup.skipped_by_phase;
                        }
                        break;

                    case SearchBranchPhase::Completed:
                        break;
                }

                for_each_timed_ride_successor(visitor);
                (void)walk_rejection_visitor;
            }
        };

        template <typename Visitor, typename RejectedWalkVisitor>
        void for_each_successor(
              const PreprocessedNetwork& network
            , const DayLevelSupplySearchGraph* day_graph
            , ZoneId                      origin
            , const ActiveDestinationMembership& active_destinations
            , const SearchBranch&        branch
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
            , TaskSearchStats*           stats
            , Visitor&&                  visit
            , RejectedWalkVisitor&&      reject_walk
        ) {
            auto&& visitor = visit;
            auto&& walk_rejection_visitor = reject_walk;
            if (day_graph != nullptr) {
                for_each_day_level_supply_successor(
                      *day_graph
                    , network
                    , origin
                    , active_destinations
                    , branch
                    , limits
                    , first_departure_domain
                    , visitor
                    , walk_rejection_visitor
                );
                return;
            }

            PaperSuccessorGenerator{
                  .network                = network
                , .origin                 = origin
                , .active_destinations    = active_destinations
                , .branch                 = branch
                , .limits                 = limits
                , .first_departure_domain = first_departure_domain
                , .stats                  = stats
            }.for_each(visitor, walk_rejection_visitor);
        }

        std::vector<ConnectionSegmentId> connection_segments_of(
            const ConnectionTrace& trace
        ) {
            std::vector<ConnectionSegmentId> segments;
            segments.reserve(trace.legs.size());
            for (const auto& leg : trace.legs) {
                if (leg.connection_segment.has_value()) {
                    segments.push_back(*leg.connection_segment);
                }
            }
            return segments;
        }

        std::vector<ConnectionSegmentId> branch_connection_segments(
              const BranchArena&  branches
            , const SearchBranch& branch
        ) {
            if (branch.od_day_carrier.support_prefix != nullptr) {
                return materialize_od_day_support_segments(
                    branch.od_day_carrier.support_prefix
                );
            }

            std::vector<ConnectionSegmentId> reversed_segments;
            const auto* cursor = &branch;
            while (cursor->trace.incoming_segment.has_value()) {
                reversed_segments.push_back(*cursor->trace.incoming_segment);
                if (!cursor->trace.parent_branch.has_value()) {
                    break;
                }
                cursor = &branch_at(branches, *cursor->trace.parent_branch);
            }
            std::reverse(reversed_segments.begin(), reversed_segments.end());
            return reversed_segments;
        }

        [[nodiscard]] std::optional<std::size_t> first_timed_segment_position(
              const PreprocessedNetwork&              network
            , std::span<const ConnectionSegmentId>     segments
        ) {
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (!is_walk_connection(connection_segment_at(network, segments[i]))) {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] Time access_time_before_first_timed(
              const PreprocessedNetwork&              network
            , std::span<const ConnectionSegmentId>     segments
            , std::size_t                             first_timed_pos
        ) {
            Time access_time{ 0.0 };
            for (std::size_t i = 0; i < first_timed_pos; ++i) {
                const auto& segment = connection_segment_at(network, segments[i]);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                access_time = add_time(access_time, route_segment.run_time);
            }
            return access_time;
        }

        mathfp::Expected<ConnectionTrace> materialize_connection_trace(
              const BranchArena&         branches
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
        ) {
            const auto segments = branch_connection_segments(branches, branch);
            const auto first_timed_pos = first_timed_segment_position(network, segments);
            if (!first_timed_pos.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("completed search branch has no timed segment")
                        .ctx("origin", branch.trace.origin.get())
                        .ctx("endpoint_id", branch.trace.current_physical.id)
                );
            }

            const auto& first_timed_segment = connection_segment_at(
                  network
                , segments[*first_timed_pos]
            );
            if (!first_timed_segment.departure.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("first timed segment has no departure while materializing trace")
                        .ctx("segment", first_timed_segment.id.get())
                );
            }

            auto cursor = Time{
                first_timed_segment.departure->value()
                - access_time_before_first_timed(
                      network
                    , segments
                    , *first_timed_pos
                  ).value()
            };
            auto current_physical = endpoint_key(branch.trace.origin);
            bool after_first_timed = false;
            ConnectionTrace trace;
            trace.legs.reserve(segments.size() + branch.metrics.transfers.get());

            for (const auto segment_id : segments) {
                const auto& segment = connection_segment_at(network, segment_id);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                if (is_walk_connection(segment)) {
                    const auto next_physical = physical_to_key(route_segment);
                    const auto kind = after_first_timed
                        ? (next_physical.kind == EndpointKind::Zone
                            ? ConnectionLegKind::EgressWalk
                            : ConnectionLegKind::TransferWalk)
                        : ConnectionLegKind::AccessWalk;
                    auto leg = make_walk_leg(kind, segment_id, route_segment, cursor);
                    cursor = leg.end_time;
                    current_physical = leg.physical_to;
                    trace.legs.push_back(std::move(leg));
                    continue;
                }

                if (after_first_timed) {
                    if (auto wait_leg = make_transfer_wait_leg(
                          cursor
                        , segment
                        , current_physical
                    )) {
                        cursor = wait_leg->end_time;
                        trace.legs.push_back(*wait_leg);
                    }
                }

                auto ride_leg = make_ride_leg(segment, route_segment);
                cursor = ride_leg.end_time;
                current_physical = ride_leg.physical_to;
                after_first_timed = true;
                trace.legs.push_back(std::move(ride_leg));
            }

            return trace;
        }

        mathfp::Expected<std::optional<SearchConnection>> complete_connection(
              const BranchArena&         branches
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const TransferLimits&      limits
            , ZoneId                     destination
        ) {
            if (!is_complete_connection(branch, destination)) {
                return std::nullopt;
            }

            if (!limits.allow_end_wait
                && branch.trace.last_timed_segment != nullptr
                && branch.trace.incoming_segment.has_value()) {
                const auto& last_segment = connection_segment_at(
                      network
                    , branch.trace.incoming_segment.value()
                );
                if (is_walk_connection(last_segment)) {
                    return std::nullopt;
                }
            }

            MATHFP_TRY_LET(
                  ConnectionTrace
                , trace
                , materialize_connection_trace(branches, branch, network)
            );
            MATHFP_TRY_LET(
                  SearchConnection
                , connection
                , make_search_connection(
                      branch.trace.origin
                    , destination
                    , std::move(trace)
                )
            );
            return std::optional<SearchConnection>{ std::move(connection) };
        }

        [[nodiscard]] bool complete_branch_can_finish(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const TransferLimits&      limits
            , ZoneId                     destination
        ) {
            if (!is_complete_connection(branch, destination)) {
                return false;
            }

            if (!limits.allow_end_wait
                && branch.trace.last_timed_segment != nullptr
                && branch.trace.incoming_segment.has_value()) {
                const auto& last_segment = connection_segment_at(
                      network
                    , branch.trace.incoming_segment.value()
                );
                if (is_walk_connection(last_segment)) {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] mathfp::Expected<CompleteConnectionMetrics>
        complete_connection_metrics_from_branch(
              const SearchBranch&       branch
            , const SearchCostContext&  search_cost
        ) {
            if (!branch.metrics.departure.has_value()
                || !branch.metrics.current_time.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("completed branch metrics miss departure or arrival")
                        .ctx("origin", branch.trace.origin.get())
                );
            }

            const auto components = SearchCostComponents{
                  .base = partial_impedance_components(branch.metrics)
                , .capacity_exposure = branch.metrics.capacity_exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(components, search_cost)
            );
            return CompleteConnectionMetrics{
                  .departure   = *branch.metrics.departure
                , .arrival     = *branch.metrics.current_time
                , .journey_time = partial_journey_time(branch.metrics)
                , .transfers    = branch.metrics.transfers
                , .impedance    = impedance
            };
        }

        [[nodiscard]] std::size_t finalize_compact_complete_connection_count(
              const CompactCompleteConnectionRetention& retention
            , const ChoiceTolerances&                   tolerances
            , ChoiceRolloutStage                        rollout_stage
        ) noexcept {
            if (rollout_stage == ChoiceRolloutStage::ExactOnly) {
                return retention.metrics.size();
            }

            const auto summary = summarize_complete_metrics(retention);
            std::size_t count = 0;
            for (const auto& metrics : retention.metrics) {
                if (within_complete_connection_tolerances(
                      metrics
                    , summary
                    , tolerances
                )) {
                    ++count;
                }
            }
            return count;
        }

        mathfp::Expected<CompleteConnectionRetentionDecision>
        retain_exact_compact_complete_connection(
              CompactCompleteConnectionRetention& retention
            , const BranchArena&                  branches
            , const SearchBranch&                 branch
            , CompleteConnectionMetrics           metrics
            , const CompleteConnectionDominanceConfig& dominance_config
        ) {
            for (const auto& known : retention.metrics) {
                if (complete_connection_dominates(
                      dominance_config
                    , known
                    , metrics
                )) {
                    return CompleteConnectionRetentionDecision{
                          .accepted          = false
                        , .removed_dominated = 0
                    };
                }
            }

            auto trace = branch_connection_segments(branches, branch);
            for (const auto& known : retention.traces) {
                if (known == trace) {
                    return CompleteConnectionRetentionDecision{
                          .accepted          = false
                        , .removed_dominated = 0
                    };
                }
            }

            const auto before = retention.metrics.size();
            std::size_t write = 0;
            for (std::size_t read = 0; read < retention.metrics.size(); ++read) {
                if (complete_connection_dominates(
                      dominance_config
                    , metrics
                    , retention.metrics[read]
                )) {
                    continue;
                }
                if (write != read) {
                    retention.metrics[write] = retention.metrics[read];
                    retention.traces[write]  = std::move(retention.traces[read]);
                }
                ++write;
            }
            retention.metrics.resize(write);
            retention.traces.resize(write);
            const auto removed = before - retention.metrics.size();
            retention.metrics.push_back(metrics);
            retention.traces.push_back(std::move(trace));
            return CompleteConnectionRetentionDecision{
                  .accepted          = true
                , .removed_dominated = removed
            };
        }

        mathfp::Expected<SearchPruningDecision> retain_branch(
              const SearchBranch&               branch
            , NodeMetricMap&                    known_metrics
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            if (!branch.metrics.departure.has_value() || !branch.metrics.current_time.has_value()) {
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                };
            }

            ++pruning_stats.evaluated_candidates;
            MATHFP_TRY_LET(
                  PartialPruningMetrics
                , metrics
                , make_partial_pruning_metrics(branch, search_cost)
            );
            const auto node  = search_node_key(branch, pruning_execution);
            auto it          = known_metrics.find(node);
            if (it == known_metrics.end()) {
                if (stores_search_pruning_metrics(pruning_execution)) {
                    insert_pruning_metrics(
                          known_metrics
                        , pruning_execution
                        , node
                        , std::move(metrics)
                    );
                    ++pruning_stats.inserted_metrics;
                } else {
                    ++pruning_stats.skipped_insertions;
                }
                ++pruning_stats.accepted_candidates;
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                };
            }

            const auto pruning_decision = evaluate_search_pruning(
                  pruning_execution
                , metrics
                , it->second
                , params.transfers
            );
            if (!pruning_decision.accepted) {
                switch (pruning_decision.layer) {
                    case SearchPruningLayer::Exact:
                        ++pruning_stats.rejected_exact;
                        break;
                    case SearchPruningLayer::Approximate:
                        ++pruning_stats.rejected_approximate;
                        break;
                }
                return pruning_decision;
            }
            if (stores_search_pruning_metrics(pruning_execution)) {
                insert_pruning_metrics(
                      known_metrics
                    , pruning_execution
                    , node
                    , std::move(metrics)
                );
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return pruning_decision;
        }

        mathfp::Expected<SearchPruningDecision> retain_branch(
              const SearchBranch&               branch
            , SearchProjectionRetention&        retention
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            return retain_branch(
                  branch
                , retention.known_metrics
                , params
                , search_cost
                , pruning_execution
                , pruning_stats
            );
        }

        mathfp::Expected<SearchPruningDecision> retain_branch(
              const SearchBranch&               branch
            , TreePartialRetention&             retention
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            return retain_branch(
                  branch
                , retention.known_metrics
                , params
                , search_cost
                , pruning_execution
                , pruning_stats
            );
        }

        struct PaperConnectionRetentionDecision final {
            SearchPruningDecision pruning{};
            PaperConnectionLabelId label{};
            std::vector<PaperConnectionLabelId> removed_labels{};
            std::size_t removed_stale_labels{};

            [[nodiscard]] bool accepted() const noexcept {
                return pruning.accepted;
            }
        };

        mathfp::Expected<PaperConnectionRetentionDecision> retain_paper_connection_tree_node(
              PaperConnectionNodeKey            node
            , PartialPruningMetrics             metrics
            , std::optional<PaperConnectionLabelId> parent_label
            , PaperConnectionLabelRegistry&      label_registry
            , TreePartialRetention&             retention
            , const SearchParams&               params
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            //tex:
            // Paper $$C_y$$ retention at the current tree node. A candidate
            // $$c_y^*=c_x^*+s^*_{x,y}$$ is accepted only if no retained
            // $$c\in C_y$$ dominates it by $$DEP,ARR,IMP,NT$$ and the node-local
            // tolerance bounds for $$IMP,JT,NT$$ and $$MAXNT$$ are satisfied.
            // The map is created per origin batch, so $$C_y$$ contains only
            // connections that start from the same origin.
            ++pruning_stats.evaluated_candidates;
            auto it = retention.paper_connections.find(node);
            if (it == retention.paper_connections.end()) {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                std::vector<PaperConnectionLabelId> removed_labels;
                if (stores_search_pruning_metrics(pruning_execution)) {
                    insert_pruning_metrics(
                          retention.paper_connections
                        , pruning_execution
                        , node
                        , std::move(metrics)
                        , label
                        , removed_labels
                    );
                    ++pruning_stats.inserted_metrics;
                } else {
                    ++pruning_stats.skipped_insertions;
                }
                ++pruning_stats.accepted_candidates;
                return PaperConnectionRetentionDecision{
                      .pruning = SearchPruningDecision{
                          .layer    = SearchPruningLayer::Exact
                        , .reason   = SearchPruningReason::Accepted
                        , .accepted = true
                      }
                    , .label = label
                    , .removed_labels = std::move(removed_labels)
                };
            }

            const auto removed_stale_labels =
                remove_inactive_paper_node_connection_metrics(
                      it->second
                    , label_registry
                );
            if (it->second.metrics.empty()) {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                std::vector<PaperConnectionLabelId> removed_labels;
                if (stores_search_pruning_metrics(pruning_execution)) {
                    insert_pruning_metrics(
                          retention.paper_connections
                        , pruning_execution
                        , node
                        , std::move(metrics)
                        , label
                        , removed_labels
                    );
                    ++pruning_stats.inserted_metrics;
                } else {
                    ++pruning_stats.skipped_insertions;
                }
                ++pruning_stats.accepted_candidates;
                return PaperConnectionRetentionDecision{
                      .pruning = SearchPruningDecision{
                          .layer    = SearchPruningLayer::Exact
                        , .reason   = SearchPruningReason::Accepted
                        , .accepted = true
                      }
                    , .label = label
                    , .removed_labels = std::move(removed_labels)
                    , .removed_stale_labels = removed_stale_labels
                };
            }

            const auto pruning_decision = evaluate_paper_node_connection_set(
                  pruning_execution
                , metrics
                , it->second
                , params.transfers
            );
            if (!pruning_decision.accepted) {
                switch (pruning_decision.layer) {
                    case SearchPruningLayer::Exact:
                        ++pruning_stats.rejected_exact;
                        break;
                    case SearchPruningLayer::Approximate:
                        ++pruning_stats.rejected_approximate;
                        break;
                }
                return PaperConnectionRetentionDecision{
                      .pruning = pruning_decision
                    , .removed_stale_labels = removed_stale_labels
                };
            }
            if (stores_search_pruning_metrics(pruning_execution)) {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                std::vector<PaperConnectionLabelId> removed_labels;
                insert_pruning_metrics(
                      retention.paper_connections
                    , pruning_execution
                    , node
                    , std::move(metrics)
                    , label
                    , removed_labels
                );
                ++pruning_stats.inserted_metrics;
                pruning_stats.accepted_candidates++;
                return PaperConnectionRetentionDecision{
                      .pruning = pruning_decision
                    , .label = label
                    , .removed_labels = std::move(removed_labels)
                    , .removed_stale_labels = removed_stale_labels
                };
            } else {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                ++pruning_stats.skipped_insertions;
                ++pruning_stats.accepted_candidates;
                return PaperConnectionRetentionDecision{
                      .pruning = pruning_decision
                    , .label = label
                    , .removed_stale_labels = removed_stale_labels
                };
            }
        }

        mathfp::Expected<SearchPruningDecision> retain_od_day_label_branch(
              const SearchBranch&               branch
            , TreePartialRetention&             retention
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , const OdDayLabelRetentionConfig&  retention_config
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            ++pruning_stats.evaluated_candidates;
            MATHFP_TRY_LET(
                  PartialPruningMetrics
                , metrics
                , make_day_path_pruning_metrics(branch, search_cost)
            );
            const auto& support = branch.od_day_carrier.support_envelope;
            auto key = od_day_label_state(branch);
            auto it = retention.od_day_label_states.find(key);
            if (it == retention.od_day_label_states.end()) {
                if (stores_search_pruning_metrics(pruning_execution)) {
                    auto representatives = OdDayLabelRepresentativeSet{};
                    const auto retained = insert_bounded_od_day_label_representative(
                          pruning_execution
                        , representatives
                        , metrics
                        , support
                        , retention_config
                    );
                    if (!retained) {
                        ++pruning_stats.rejected_approximate;
                        return SearchPruningDecision{
                              .layer    = SearchPruningLayer::Approximate
                            , .reason   = SearchPruningReason::RejectedApproximateTolerance
                            , .accepted = false
                        };
                    }
                    retention.od_day_label_states.emplace(std::move(key), std::move(representatives));
                    ++pruning_stats.inserted_metrics;
                } else {
                    ++pruning_stats.skipped_insertions;
                }
                ++pruning_stats.accepted_candidates;
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                };
            }

            const auto compatible_metrics = compatible_od_day_label_metrics(
                  it->second
                , support
            );
            auto pruning_decision = SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = SearchPruningReason::Accepted
                , .accepted = true
            };
            if (!compatible_metrics.metrics.empty()) {
                pruning_decision = evaluate_search_pruning(
                      pruning_execution
                    , metrics
                    , compatible_metrics
                    , params.transfers
                );
            }
            if (!pruning_decision.accepted) {
                switch (pruning_decision.layer) {
                    case SearchPruningLayer::Exact:
                        ++pruning_stats.rejected_exact;
                        break;
                    case SearchPruningLayer::Approximate:
                        ++pruning_stats.rejected_approximate;
                        break;
                }
                return pruning_decision;
            }
            if (contains_od_day_label_representative(it->second, metrics, support)) {
                ++pruning_stats.rejected_exact;
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::RejectedExactDominance
                    , .accepted = false
                };
            }

            if (stores_search_pruning_metrics(pruning_execution)) {
                const auto retained = insert_bounded_od_day_label_representative(
                      pruning_execution
                    , it->second
                    , metrics
                    , support
                    , retention_config
                );
                if (!retained) {
                    ++pruning_stats.rejected_approximate;
                    return SearchPruningDecision{
                          .layer    = SearchPruningLayer::Approximate
                        , .reason   = SearchPruningReason::RejectedApproximateTolerance
                        , .accepted = false
                    };
                }
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return pruning_decision;
        }

        BranchState feasibility_state(
            const SearchBranch& branch
        ) noexcept {
            return BranchState{
                  .current_arrival_time = branch.metrics.current_time
                , .last_segment         = branch.trace.last_timed_segment
                , .last_route_segment   = branch.trace.last_timed_route_segment
                , .transfer_count       = branch.metrics.departure.has_value()
                    ? std::optional<TransferCount>{ branch.metrics.transfers }
                    : std::nullopt
            };
        }

        bool paper_connection_segment_successor_feasible_before_branch(
              const SearchBranch&         branch
            , const PreprocessedNetwork&  network
            , const ConnectionSegment&    successor
            , const RouteSegment&         successor_route_segment
            , const SearchTimeDomain*     first_departure_domain
            , const TransferLimits&       limits
            , TaskSearchStats&            stats
        ) noexcept {
            if (is_walk_connection(successor)) {
                /*
                 * Walk connection segments are always available in the paper
                 * preprocessing model. They do not have departure times; the
                 * extension only advances the current prefix time by walk
                 * duration in extend_metrics_with_walk.
                 */
                if (!is_branch_extension_feasible(
                      feasibility_state(branch)
                    , successor
                    , successor_route_segment
                    , limits
                )) {
                    ++stats.rejected_feasibility;
                    return false;
                }
                return true;
            }

            if (!first_timed_departure_allowed(
                  branch
                , successor
                , first_departure_domain
                , limits
            )) {
                ++stats.rejected_time_domain;
                return false;
            }
            if (!is_branch_extension_feasible(
                  feasibility_state(branch)
                , successor
                , successor_route_segment
                , limits
            )) {
                ++stats.rejected_feasibility;
                return false;
            }
            if (!improves_repeated_stop_reboarding(
                  branch
                , network
                , successor
                , successor_route_segment
            )) {
                ++stats.rejected_reboarding;
                return false;
            }
            return true;
        }

        bool paper_search_successor_feasible_before_branch(
              const SearchBranch&         branch
            , const PreprocessedNetwork&  network
            , const SearchSuccessor&      successor_ref
            , const SearchTimeDomain*     first_departure_domain
            , const TransferLimits&       limits
            , TaskSearchStats&            stats
        ) noexcept {
            const auto& successor = connection_segment_at(network, successor_ref.connection);
            const auto& successor_route_segment = route_segment_at(
                  network
                , successor.route_segment
            );

            return paper_connection_segment_successor_feasible_before_branch(
                  branch
                , network
                , successor
                , successor_route_segment
                , first_departure_domain
                , limits
                , stats
            );
        }

        [[nodiscard]] std::size_t retained_connection_count(
            std::span<const SearchProjectionRetention> retentions
        ) noexcept {
            std::size_t total = 0;
            for (const auto& retention : retentions) {
                total += retention.complete_connections.alternatives.size();
                total += retention.compact_complete_connections.metrics.size();
            }
            return total;
        }

        [[nodiscard]] std::size_t retained_day_path_count(
            std::span<const SearchProjectionRetention> retentions
        ) noexcept {
            std::size_t total = 0;
            for (const auto& retention : retentions) {
                total += day_path_retention_size(retention.day_paths);
            }
            return total;
        }

        struct SearchStorageDiagnostics final {
            std::size_t branch_slots{};
            std::size_t live_branches{};
            std::size_t released_branches{};
            std::size_t projection_states{};
            std::size_t tree_pruning_nodes{};
            std::size_t tree_pruning_buckets{};
            float       tree_pruning_load_factor{};
            std::size_t tree_pruning_metrics{};
            std::size_t tree_pruning_labels{};
            std::size_t od_day_label_state_nodes{};
            std::size_t od_day_label_state_buckets{};
            float       od_day_label_state_load_factor{};
            std::size_t od_day_label_representatives{};
            std::size_t tree_pruning_insertions{};
            std::size_t retained_complete_connections{};
            std::size_t retained_day_paths{};
            std::size_t approximate_direct_bytes{};
        };

        [[nodiscard]] SearchStorageDiagnostics search_storage_diagnostics(
              const BranchArena&                         branches
            , std::size_t                                released_branches
            , std::size_t                                projection_state_count
            , std::size_t                                projection_state_size
            , const TreePartialRetention&                tree_retention
            , std::span<const SearchProjectionRetention> retentions
            , const SearchPruningRuntimeStats&           pruning_stats
        ) noexcept {
            const auto pruning_nodes = tree_retention.paper_connections.size();
            const auto pruning_buckets = tree_retention.paper_connections.bucket_count();
            std::size_t pruning_metrics = 0u;
            std::size_t pruning_labels = 0u;
            for (const auto& [node, metric_set] : tree_retention.paper_connections) {
                (void)node;
                pruning_metrics += metric_set.metrics.size();
                pruning_labels += metric_set.labels.size();
            }
            const auto od_day_label_state_nodes = tree_retention.od_day_label_states.size();
            const auto od_day_label_state_buckets = tree_retention.od_day_label_states.bucket_count();
            std::size_t od_day_label_representatives = 0u;
            for (const auto& [state, representative_set] : tree_retention.od_day_label_states) {
                (void)state;
                od_day_label_representatives += representative_set.representatives.size();
            }
            const auto live_branches = branches.size() - released_branches;
            std::size_t compact_complete_metrics = 0;
            std::size_t day_path_alternatives = 0;
            for (const auto& retention : retentions) {
                compact_complete_metrics += retention.compact_complete_connections.metrics.size();
                day_path_alternatives += day_path_retention_size(retention.day_paths);
            }
            const auto approximate_direct_bytes =
                  branches.size() * sizeof(BranchSlot)
                + live_branches * sizeof(SearchBranch)
                + branches.size() * (
                      sizeof(OdDayPathPrefixNode)
                    + sizeof(OdDaySupportPrefixNode)
                  )
                + projection_state_count * projection_state_size
                + pruning_nodes * sizeof(PaperConnectionNodeMetricMap::value_type)
                + pruning_buckets * sizeof(void*)
                + pruning_metrics * sizeof(SearchPruningMetrics)
                + pruning_labels * sizeof(PaperConnectionLabelId)
                + od_day_label_state_nodes * sizeof(OdDayLabelStateMap::value_type)
                + od_day_label_state_buckets * sizeof(void*)
                + od_day_label_representatives * sizeof(OdDayLabelRepresentative)
                + compact_complete_metrics * sizeof(CompleteConnectionMetrics)
                + day_path_alternatives * sizeof(DayPathAlternative);
            return SearchStorageDiagnostics{
                  .branch_slots = branches.size()
                , .live_branches = live_branches
                , .released_branches = released_branches
                , .projection_states = projection_state_count
                , .tree_pruning_nodes = pruning_nodes
                , .tree_pruning_buckets = pruning_buckets
                , .tree_pruning_load_factor = tree_retention.paper_connections.load_factor()
                , .tree_pruning_metrics = pruning_metrics
                , .tree_pruning_labels = pruning_labels
                , .od_day_label_state_nodes = od_day_label_state_nodes
                , .od_day_label_state_buckets = od_day_label_state_buckets
                , .od_day_label_state_load_factor = tree_retention.od_day_label_states.load_factor()
                , .od_day_label_representatives = od_day_label_representatives
                , .tree_pruning_insertions = pruning_stats.inserted_metrics
                , .retained_complete_connections = retained_connection_count(retentions)
                , .retained_day_paths = retained_day_path_count(retentions)
                , .approximate_direct_bytes = approximate_direct_bytes
            };
        }

        [[nodiscard]] std::string format_search_storage_diagnostics(
            const SearchStorageDiagnostics& diagnostics
        ) {
            return fmt::format(
                  "search storage: branch_slots={} live_branches={} released_branches={} projection_states={} c_y(nodes/buckets/load/metrics/labels/insertions)={}/{}/{:.3f}/{}/{}/{} path_identity=compact_prefix support=compact_prefix legacy_od_day_label_states(nodes/buckets/load/reps)={}/{}/{:.3f}/{} retained_complete={} post_layer_day_paths={} approx_direct_mb={:.2f}"
                , diagnostics.branch_slots
                , diagnostics.live_branches
                , diagnostics.released_branches
                , diagnostics.projection_states
                , diagnostics.tree_pruning_nodes
                , diagnostics.tree_pruning_buckets
                , diagnostics.tree_pruning_load_factor
                , diagnostics.tree_pruning_metrics
                , diagnostics.tree_pruning_labels
                , diagnostics.tree_pruning_insertions
                , diagnostics.od_day_label_state_nodes
                , diagnostics.od_day_label_state_buckets
                , diagnostics.od_day_label_state_load_factor
                , diagnostics.od_day_label_representatives
                , diagnostics.retained_complete_connections
                , diagnostics.retained_day_paths
                , static_cast<double>(diagnostics.approximate_direct_bytes)
                    / (1024.0 * 1024.0)
            );
        }

        [[nodiscard]] std::string format_od_day_theory_diagnostics(
              const SearchStorageDiagnostics& diagnostics
            , const TaskSearchStats&          stats
            , std::size_t                     current_frontier
            , std::size_t                     next_frontier
            , const OdDayLabelRetentionConfig& retention_config
        ) {
            return fmt::format(
                  "OD-day theory diagnostics: paper=connection_segment_tree carrier=compact_connection_segment_prefix branch_projection_state=none reachability_prefilter=disabled_not_built reachability_masks=disabled c_y=network_node_known_connections c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink dominance=dep_arr_imp_nt tolerance=node_local tree_bounds=c_y_before_day_path_sink frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_contract=single_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available transfer_walk=first_class_segment composite_transfer_walk=disabled_in_production late_guard=assert_only day_path_retention=immediate_day_path_projection suffix_bound=disabled_for_od_day walk_lookup=lazy_phase_specific live_branches={} frontier={} projection_states={} c_y_nodes={} c_y_metrics={} c_y_labels={} c_y_removed(dominated/stale)={}/{} frontier_compaction(runs/removed/current/next)={}/{}/{}/{} stale_frontier_skipped={} post_layer_day_paths={} post_layer(candidates/inserted/replaced/max_supports)={}/{}/{}/{} enqueued_after_c_y={} suffix_bound_pruned_legacy={} c_y_pruned={} paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{} late_guard(time_domain/feasibility/reboarding)={}/{}/{} walk_lookup_counts({}) legacy_od_label_states={} legacy_label_reps={} max_reps_per_state={}"
                , diagnostics.live_branches
                , current_frontier + next_frontier
                , diagnostics.projection_states
                , diagnostics.tree_pruning_nodes
                , diagnostics.tree_pruning_metrics
                , diagnostics.tree_pruning_labels
                , stats.c_y_removed_dominated
                , stats.c_y_removed_stale
                , stats.frontier_compaction_runs
                , stats.frontier_compaction_removed
                , stats.frontier_compaction_removed_current
                , stats.frontier_compaction_removed_next
                , stats.stale_frontier_skipped
                , diagnostics.retained_day_paths
                , stats.post_layer_day_path_candidates
                , stats.post_layer_day_path_inserted
                , stats.post_layer_day_path_representative_replaced
                , stats.post_layer_day_path_supports
                , stats.accepted_branches
                , stats.rejected_suffix_lower_bound
                , stats.rejected_dominance_or_tolerance
                , stats.paper_timed_lookup_skipped_phase
                , stats.paper_timed_lookup_skipped_transfer_budget
                , stats.paper_timed_successor_rejected_time_domain
                , stats.paper_timed_successor_rejected_same_trip
                , stats.paper_timed_successor_rejected_same_line
                , stats.paper_timed_successor_rejected_feasibility
                , stats.rejected_time_domain
                , stats.rejected_feasibility
                , stats.rejected_reboarding
                , format_walk_lookup_stats(stats.walk_lookup)
                , diagnostics.od_day_label_state_nodes
                , diagnostics.od_day_label_representatives
                , retention_config.max_representatives_per_label
            );
        }

        [[nodiscard]] std::string format_optional_size_limit(
            std::optional<std::size_t> limit
        ) {
            return limit.has_value()
                ? std::to_string(*limit)
                : std::string("unbounded");
        }

        [[nodiscard]] std::string format_optional_mb_limit(
            std::optional<std::size_t> limit
        ) {
            return limit.has_value()
                ? fmt::format("{:.2f}", static_cast<double>(*limit) / (1024.0 * 1024.0))
                : std::string("unbounded");
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_production_batch_invariants(
              const SearchBatch&               batch
            , const TaskSearchStats&           stats
            , const SearchStorageDiagnostics&  storage
        ) {
            if (storage.projection_states != 0u) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch retained branch projection states")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("projection_states", static_cast<std::int64_t>(storage.projection_states))
                );
            }
            if (storage.retained_complete_connections != 0u) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch retained raw complete alternatives")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "retained_complete_connections"
                            , static_cast<std::int64_t>(storage.retained_complete_connections)
                          )
                );
            }
            if (storage.tree_pruning_metrics != storage.tree_pruning_labels) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production C_y metrics and frontier labels are not synchronized")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "c_y_metrics"
                            , static_cast<std::int64_t>(storage.tree_pruning_metrics)
                          )
                        .ctx(
                              "c_y_labels"
                            , static_cast<std::int64_t>(storage.tree_pruning_labels)
                          )
                );
            }
            if (storage.od_day_label_state_nodes != 0u
                || storage.od_day_label_representatives != 0u) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch used legacy OD-day label storage")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "legacy_label_nodes"
                            , static_cast<std::int64_t>(storage.od_day_label_state_nodes)
                          )
                        .ctx(
                              "legacy_label_representatives"
                            , static_cast<std::int64_t>(storage.od_day_label_representatives)
                          )
                );
            }
            if (stats.completed_connections != stats.post_layer_day_path_candidates) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day completed branches were not fully projected to DayPath post-layer")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "completed_connections"
                            , static_cast<std::int64_t>(stats.completed_connections)
                          )
                        .ctx(
                              "post_layer_candidates"
                            , static_cast<std::int64_t>(stats.post_layer_day_path_candidates)
                          )
                );
            }
            if (stats.post_layer_day_path_inserted > stats.post_layer_day_path_candidates
                || storage.retained_day_paths > stats.post_layer_day_path_candidates) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day DayPath post-layer counters are inconsistent")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "post_layer_candidates"
                            , static_cast<std::int64_t>(stats.post_layer_day_path_candidates)
                          )
                        .ctx(
                              "post_layer_inserted"
                            , static_cast<std::int64_t>(stats.post_layer_day_path_inserted)
                          )
                        .ctx(
                              "retained_day_paths"
                            , static_cast<std::int64_t>(storage.retained_day_paths)
                          )
                );
            }
            if (stats.rejected_reachability != 0u
                || stats.rejected_suffix_lower_bound != 0u) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch used disabled reachability/suffix pruning")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "reachability_rejections"
                            , static_cast<std::int64_t>(stats.rejected_reachability)
                          )
                        .ctx(
                              "suffix_lower_bound_rejections"
                            , static_cast<std::int64_t>(stats.rejected_suffix_lower_bound)
                          )
                );
            }
            if (stats.rejected_time_domain != 0u
                || stats.rejected_feasibility != 0u
                || stats.rejected_reboarding != 0u) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production successor generator emitted non-insertable paper successor")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "late_time_domain"
                            , static_cast<std::int64_t>(stats.rejected_time_domain)
                          )
                        .ctx(
                              "late_feasibility"
                            , static_cast<std::int64_t>(stats.rejected_feasibility)
                          )
                        .ctx(
                              "late_reboarding"
                            , static_cast<std::int64_t>(stats.rejected_reboarding)
                          )
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_production_memory_limits(
              const SearchStorageDiagnostics& diagnostics
            , std::size_t                     current_frontier
            , std::size_t                     next_frontier
            , const OdDayProductionMemoryLimits& limits
            , ZoneId                          origin
        ) {
            const auto fail =
                [&](const char* metric, std::size_t actual, std::size_t limit)
                    -> mathfp::Expected<mathfp::Unit> {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production memory limit exceeded")
                            .ctx("origin", origin.get())
                            .ctx("metric", std::string(metric))
                            .ctx("actual", static_cast<std::int64_t>(actual))
                            .ctx("limit", static_cast<std::int64_t>(limit))
                    );
                };

            if (limits.max_branch_slots_per_tree.has_value()
                && diagnostics.branch_slots > *limits.max_branch_slots_per_tree) {
                return fail(
                      "branch_slots"
                    , diagnostics.branch_slots
                    , *limits.max_branch_slots_per_tree
                );
            }
            if (limits.max_live_branches_per_tree.has_value()
                && diagnostics.live_branches > *limits.max_live_branches_per_tree) {
                return fail(
                      "live_branches"
                    , diagnostics.live_branches
                    , *limits.max_live_branches_per_tree
                );
            }
            const auto frontier = current_frontier + next_frontier;
            if (limits.max_frontier_per_tree.has_value()
                && frontier > *limits.max_frontier_per_tree) {
                return fail("frontier", frontier, *limits.max_frontier_per_tree);
            }
            if (limits.max_od_day_label_states_per_tree.has_value()
                && diagnostics.od_day_label_state_nodes
                    > *limits.max_od_day_label_states_per_tree) {
                return fail(
                      "od_day_label_states"
                    , diagnostics.od_day_label_state_nodes
                    , *limits.max_od_day_label_states_per_tree
                );
            }
            if (limits.max_retained_day_paths_per_tree.has_value()
                && diagnostics.retained_day_paths > *limits.max_retained_day_paths_per_tree) {
                return fail(
                      "post_layer_day_paths"
                    , diagnostics.retained_day_paths
                    , *limits.max_retained_day_paths_per_tree
                );
            }
            if (limits.max_approximate_direct_bytes_per_tree.has_value()
                && diagnostics.approximate_direct_bytes
                    > *limits.max_approximate_direct_bytes_per_tree) {
                return fail(
                      "approximate_direct_bytes"
                    , diagnostics.approximate_direct_bytes
                    , *limits.max_approximate_direct_bytes_per_tree
                );
            }

            return mathfp::kUnit;
        }

        struct ReachabilityTaskFilter final {
            ActiveIndexSet reachable{};
            std::vector<RejectedReachabilityTask> unreachable{};
        };

        struct CompactReachabilityReasons final {
            static constexpr std::size_t inline_capacity = FixedActiveMask::max_size;
            static constexpr std::size_t bits_per_reason = 2u;
            static constexpr std::size_t reasons_per_word = 64u / bits_per_reason;
            static constexpr std::size_t inline_word_count =
                (inline_capacity + reasons_per_word - 1u) / reasons_per_word;

            std::size_t size{};
            std::array<std::uint64_t, inline_word_count> inline_words{};
            std::vector<std::uint64_t> heap_words{};

            CompactReachabilityReasons() = default;

            explicit CompactReachabilityReasons(
                  std::size_t                 element_count
                , ReachabilityRejectionReason initial_reason
            )
                : size{ element_count }
            {
                if (!uses_inline_storage()) {
                    heap_words.assign(word_count(), 0u);
                }
                for (std::size_t i = 0; i < size; ++i) {
                    set(i, initial_reason);
                }
            }

            [[nodiscard]] bool uses_inline_storage() const noexcept {
                return size <= inline_capacity;
            }

            [[nodiscard]] std::size_t word_count() const noexcept {
                return (size + reasons_per_word - 1u) / reasons_per_word;
            }

            [[nodiscard]] std::uint64_t word(std::size_t index) const noexcept {
                return uses_inline_storage()
                    ? inline_words[index]
                    : heap_words[index];
            }

            [[nodiscard]] std::uint64_t& word(std::size_t index) noexcept {
                return uses_inline_storage()
                    ? inline_words[index]
                    : heap_words[index];
            }

            [[nodiscard]] static std::uint64_t reason_code(
                ReachabilityRejectionReason reason
            ) noexcept {
                switch (reason) {
                    case ReachabilityRejectionReason::Phase:
                        return 0u;
                    case ReachabilityRejectionReason::TransferBudget:
                        return 1u;
                    case ReachabilityRejectionReason::UnreachableDestination:
                        return 2u;
                }
                return 2u;
            }

            [[nodiscard]] static ReachabilityRejectionReason reason_from_code(
                std::uint64_t code
            ) noexcept {
                switch (code) {
                    case 0u:
                        return ReachabilityRejectionReason::Phase;
                    case 1u:
                        return ReachabilityRejectionReason::TransferBudget;
                    case 2u:
                    default:
                        return ReachabilityRejectionReason::UnreachableDestination;
                }
            }

            [[nodiscard]] ReachabilityRejectionReason get(
                std::size_t index
            ) const noexcept {
                const auto word_index = index / reasons_per_word;
                const auto bit_offset = (index % reasons_per_word) * bits_per_reason;
                return reason_from_code(
                    (word(word_index) >> bit_offset) & std::uint64_t{ 0b11 }
                );
            }

            void set(
                  std::size_t                 index
                , ReachabilityRejectionReason reason
            ) noexcept {
                if (index >= size) {
                    return;
                }
                const auto word_index = index / reasons_per_word;
                const auto bit_offset = (index % reasons_per_word) * bits_per_reason;
                const auto mask = std::uint64_t{ 0b11 } << bit_offset;
                auto& target_word = word(word_index);
                target_word =
                    (target_word & ~mask)
                    | (reason_code(reason) << bit_offset);
            }
        };

        struct ReachabilityMaskEntry final {
            ActiveIndexSet reachable{};
            CompactReachabilityReasons rejection_reasons{};
        };

        [[nodiscard]] bool completion_target_projection_slots(
            std::span<const SearchProjectionSlot> slots
        ) noexcept {
            return std::all_of(
                  slots.begin()
                , slots.end()
                , [](const SearchProjectionSlot& slot) {
                      return slot.kind == SearchProjectionSlotKind::CompletionTarget;
                  }
            );
        }

        [[nodiscard]] bool od_day_projection_slots(
            std::span<const SearchProjectionSlot> slots
        ) noexcept {
            return std::all_of(
                  slots.begin()
                , slots.end()
                , [](const SearchProjectionSlot& slot) {
                      return slot.kind == SearchProjectionSlotKind::OdDayPair;
                  }
            );
        }

        [[nodiscard]] bool has_projection_slot_kind(
              std::span<const SearchProjectionSlot> slots
            , SearchProjectionSlotKind              kind
        ) noexcept {
            return std::any_of(
                  slots.begin()
                , slots.end()
                , [kind](const SearchProjectionSlot& slot) {
                      return slot.kind == kind;
                  }
            );
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_production_batch_runtime_contract(
              const SearchBatch&                batch
            , SearchPartialRetentionScope       partial_retention_scope
            , const SearchPruningExecutionPlan& pruning_execution
            , const DayLevelSupplySearchGraph*  day_level_supply
        ) {
            const auto slots = std::span<const SearchProjectionSlot>{
                  batch.projection_slots.data()
                , batch.projection_slots.size()
            };
            const auto has_od_day_slots = has_projection_slot_kind(
                  slots
                , SearchProjectionSlotKind::OdDayPair
            );
            if (!has_od_day_slots) {
                return mathfp::kUnit;
            }
            if (day_level_supply != nullptr) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production search must use paper connection-segment carrier, not structural day-level graph")
                        .ctx("origin", batch.key.origin.get())
                );
            }
            if (!od_day_projection_slots(slots)) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch cannot mix day-path slots with timed projection slots")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                );
            }
            if (batch.key.interval.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch must cover the whole service day, not one demand interval")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("interval", batch.key.interval->get())
                );
            }
            if (partial_retention_scope != SearchPartialRetentionScope::TreeGlobal) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production search requires tree-global paper C_y retention")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("partial_retention_scope", std::string(to_string(partial_retention_scope)))
                );
            }
            if (!pruning_execution.exact_enabled
                || !pruning_execution.approximate_enabled
                || !pruning_execution.approximate_policy.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production search requires paper-level node-local C_y relevance and tolerance retention")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("exact_enabled", pruning_execution.exact_enabled ? "true" : "false")
                        .ctx("approximate_enabled", pruning_execution.approximate_enabled ? "true" : "false")
                );
            }
            if (batch.completion_targets.size() != batch.projection_slots.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch must have one completion target per OD path slot")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("completion_targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                        .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                );
            }
            if (batch.departure_domain == nullptr) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch must carry the service-day first-boarding domain")
                        .ctx("origin", batch.key.origin.get())
                );
            }
            if (batch.departure_domain->windows.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production first-boarding domain must not be empty")
                        .ctx("origin", batch.key.origin.get())
                );
            }
            for (std::size_t slot_pos = 0; slot_pos < batch.projection_slots.size(); ++slot_pos) {
                const auto& slot = batch.projection_slots[slot_pos];
                if (!slot.completion_target.has_value()
                    || slot.completion_target->get() != static_cast<std::int64_t>(slot_pos)
                    || slot.task != nullptr
                    || slot.task_ref.has_value()
                    || slot.result_index.has_value()
                    || slot.interval.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production slot carries timed/demand projection payload")
                            .ctx("origin", batch.key.origin.get())
                            .ctx("slot_position", static_cast<std::int64_t>(slot_pos))
                    );
                }
                if (batch.completion_targets[slot_pos].destination != slot.destination) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production slot destination disagrees with its completion target")
                            .ctx("origin", batch.key.origin.get())
                            .ctx("slot_position", static_cast<std::int64_t>(slot_pos))
                            .ctx("slot_destination", slot.destination.get())
                            .ctx("target_destination", batch.completion_targets[slot_pos].destination.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_production_batches(
              std::span<const SearchBatch> batches
            , std::size_t                  expected_tree_count
            , SearchDestinationScope       destination_scope
            , std::size_t                  declared_destination_count
        ) {
            if (batches.size() != expected_tree_count) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production must have exactly one batch/tree per declared origin")
                        .ctx("batches", static_cast<std::int64_t>(batches.size()))
                        .ctx("expected_tree_count", static_cast<std::int64_t>(expected_tree_count))
                );
            }
            for (std::size_t batch_pos = 0; batch_pos < batches.size(); ++batch_pos) {
                const auto& batch = batches[batch_pos];
                const auto slots = std::span<const SearchProjectionSlot>{
                      batch.projection_slots.data()
                    , batch.projection_slots.size()
                };
                if (!od_day_projection_slots(slots)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production batch contains non-OD-day projection slots")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                    );
                }
                if (batch.key.interval.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production batch key must not be demand-interval-local")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                            .ctx("interval", batch.key.interval->get())
                    );
                }
                if (batch.completion_targets.size() != batch.projection_slots.size()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production completion targets and OD sinks disagree")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                            .ctx("targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                            .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                    );
                }
                if (destination_scope == SearchDestinationScope::DeclaredZones
                    && batch.completion_targets.size() != declared_destination_count) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production tree must target all declared destination zones")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                            .ctx("targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                            .ctx("declared_destinations", static_cast<std::int64_t>(declared_destination_count))
                    );
                }
                for (std::size_t slot_pos = 0; slot_pos < batch.projection_slots.size(); ++slot_pos) {
                    const auto& slot = batch.projection_slots[slot_pos];
                    if (slot.origin != batch.key.origin) {
                        return mathfp::unexpected(
                            mathfp::internal_error("OD-day production slot origin disagrees with tree origin")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("slot", static_cast<std::int64_t>(slot_pos))
                                .ctx("tree_origin", batch.key.origin.get())
                                .ctx("slot_origin", slot.origin.get())
                        );
                    }
                    if (!slot.completion_target.has_value()
                        || slot.completion_target->get() != static_cast<std::int64_t>(slot_pos)
                        || batch.completion_targets[slot_pos].destination != slot.destination) {
                        return mathfp::unexpected(
                            mathfp::internal_error("OD-day production sink is not aligned with completion target")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("origin", batch.key.origin.get())
                                .ctx("slot", static_cast<std::int64_t>(slot_pos))
                        );
                    }
                }
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] ResidualReachabilityKey reachability_mask_key(
              const SearchBranch&   branch
            , const TransferLimits& limits
        ) noexcept {
            return ResidualReachabilityKey{
                  .current_physical    = branch.trace.current_physical
                , .phase               = branch.trace.phase
                , .remaining_transfers = remaining_transfer_budget(branch, limits)
            };
        }

        [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
              const ResidualReachabilityKey& key
            , ZoneId                         destination
        ) noexcept {
            return RelaxedSuffixState{
                  .current_physical    = key.current_physical
                , .destination         = destination
                , .phase               = key.phase
                , .remaining_transfers = key.remaining_transfers
            };
        }

        [[nodiscard]] ReachabilityMaskEntry build_target_reachability_mask(
              const ResidualReachabilityKey&          key
            , std::span<const SearchCompletionTarget> targets
            , const ResidualReachability&             reachability
            , TransferCount                           max_transfers
        ) {
            ReachabilityMaskEntry result{
                  .reachable = ActiveIndexSet{ targets.size() }
                , .rejection_reasons = CompactReachabilityReasons{
                      targets.size()
                    , ReachabilityRejectionReason::UnreachableDestination
                  }
            };
            for (std::size_t target_pos = 0; target_pos < targets.size(); ++target_pos) {
                const auto decision = evaluate_residual_reachability(
                      reachability
                    , relaxed_suffix_state(key, targets[target_pos].destination)
                    , max_transfers
                );
                if (decision.feasible) {
                    result.reachable.set(target_pos);
                } else {
                    result.rejection_reasons.set(
                          target_pos
                        , decision.rejection_reason
                    );
                }
            }
            return result;
        }

        [[nodiscard]] ReachabilityMaskEntry build_slot_reachability_mask(
              const ResidualReachabilityKey&       key
            , std::span<const SearchProjectionSlot> slots
            , const ResidualReachability&          reachability
            , TransferCount                        max_transfers
        ) {
            ReachabilityMaskEntry result{
                  .reachable = ActiveIndexSet{ slots.size() }
                , .rejection_reasons = CompactReachabilityReasons{
                      slots.size()
                    , ReachabilityRejectionReason::UnreachableDestination
                  }
            };
            for (std::size_t task_pos = 0; task_pos < slots.size(); ++task_pos) {
                const auto decision = evaluate_residual_reachability(
                      reachability
                    , relaxed_suffix_state(key, slots[task_pos].destination)
                    , max_transfers
                );
                if (decision.feasible) {
                    result.reachable.set(task_pos);
                } else {
                    result.rejection_reasons.set(
                          task_pos
                        , decision.rejection_reason
                    );
                }
            }
            return result;
        }

        struct ReachabilityMaskCache final {
            const ResidualReachability&             reachability;
            std::span<const SearchCompletionTarget> targets;
            std::span<const SearchProjectionSlot>   slots;
            TransferCount                           max_transfers;
            bool                                    unified_completion_targets{};
            std::unordered_map<
                  ResidualReachabilityKey
                , ReachabilityMaskEntry
                , ResidualReachabilityKeyHash
            > target_masks{};
            std::unordered_map<
                  ResidualReachabilityKey
                , ReachabilityMaskEntry
                , ResidualReachabilityKeyHash
            > slot_masks{};

            const ReachabilityMaskEntry& target_entry(
                const ResidualReachabilityKey& key
            ) {
                const auto existing = target_masks.find(key);
                if (existing != target_masks.end()) {
                    return existing->second;
                }
                const auto [it, inserted] = target_masks.emplace(
                      key
                    , build_target_reachability_mask(
                          key
                        , targets
                        , reachability
                        , max_transfers
                      )
                );
                (void)inserted;
                return it->second;
            }

            const ReachabilityMaskEntry& slot_entry(
                const ResidualReachabilityKey& key
            ) {
                if (unified_completion_targets) {
                    return target_entry(key);
                }
                const auto existing = slot_masks.find(key);
                if (existing != slot_masks.end()) {
                    return existing->second;
                }
                const auto [it, inserted] = slot_masks.emplace(
                      key
                    , build_slot_reachability_mask(
                          key
                        , slots
                        , reachability
                        , max_transfers
                      )
                );
                (void)inserted;
                return it->second;
            }
        };

        [[nodiscard]] ActiveIndexSet filter_target_positions_by_reachability(
              const ActiveIndexSet&                     target_positions
            , const ReachabilityMaskEntry&              reachability_entry
        ) {
            return target_positions.intersect(reachability_entry.reachable);
        }

        [[nodiscard]] ReachabilityRejectionReason summarize_target_reachability_rejection(
              const ActiveIndexSet&        target_positions
            , const ReachabilityMaskEntry& reachability_entry
        ) noexcept {
            const auto priority = [](ReachabilityRejectionReason reason) noexcept {
                switch (reason) {
                    case ReachabilityRejectionReason::UnreachableDestination:
                        return 3;
                    case ReachabilityRejectionReason::TransferBudget:
                        return 2;
                    case ReachabilityRejectionReason::Phase:
                        return 1;
                }
                return 0;
            };
            auto result = ReachabilityRejectionReason::Phase;
            target_positions.for_each_difference_index(reachability_entry.reachable, [&](std::size_t target_pos) {
                const auto reason = reachability_entry.rejection_reasons.get(target_pos);
                if (priority(reason) > priority(result)) {
                    result = reason;
                }
            });
            return result;
        }

        [[nodiscard]] ReachabilityTaskFilter filter_task_positions_by_reachability(
              const ActiveIndexSet&        task_positions
            , const ReachabilityMaskEntry& reachability_entry
        ) {
            ReachabilityTaskFilter result{
                  .reachable = task_positions.intersect(reachability_entry.reachable)
            };
            const auto active_count = task_positions.active_count();
            const auto reachable_count = result.reachable.active_count();
            result.unreachable.reserve(active_count - reachable_count);
            task_positions.for_each_difference_index(reachability_entry.reachable, [&](std::size_t task_pos) {
                result.unreachable.push_back(
                    RejectedReachabilityTask{
                          .task_position = task_pos
                        , .reason        = reachability_entry.rejection_reasons.get(task_pos)
                    }
                );
            });
            return result;
        }

        void record_reachability_rejections(
              std::span<const RejectedReachabilityTask> rejected_tasks
            , std::vector<TaskSearchStats>& task_stats
            , TaskSearchStats&              stats
        ) noexcept {
            for (const auto rejected : rejected_tasks) {
                add_reachability_rejection(stats, rejected.reason);
                add_reachability_rejection(
                      task_stats[rejected.task_position]
                    , rejected.reason
                );
            }
        }

        void record_suffix_lower_bound_rejections(
              std::span<const RejectedSuffixLowerBoundTask> rejected_tasks
            , std::vector<TaskSearchStats>&                 task_stats
            , TaskSearchStats&                              stats
        ) noexcept {
            for (const auto rejected : rejected_tasks) {
                add_suffix_lower_bound_rejection(stats, rejected.reason);
                add_suffix_lower_bound_rejection(
                      task_stats[rejected.task_position]
                    , rejected.reason
                );
            }
        }

        [[nodiscard]] std::vector<std::size_t> matching_complete_tasks(
              const SearchBranch&              branch
            , const ActiveIndexSet&             active_tasks
            , std::span<const SearchProjectionSlot> batch_tasks
        ) {
            std::vector<std::size_t> matches;
            if (!branch.metrics.departure.has_value()
                || branch.trace.current_physical.kind != EndpointKind::Zone) {
                return matches;
            }

            active_tasks.for_each_index([&](std::size_t task_pos) {
                if (batch_tasks[task_pos].destination.get()
                    == branch.trace.current_physical.id) {
                    matches.push_back(task_pos);
                }
            });
            return matches;
        }

        [[nodiscard]] bool matches_completion_target(
              const SearchBranch&                       branch
            , const ActiveIndexSet&                      active_targets
            , std::span<const SearchCompletionTarget>    batch_targets
        ) noexcept {
            if (!branch.metrics.departure.has_value()
                || branch.trace.current_physical.kind != EndpointKind::Zone) {
                return false;
            }

            bool matches = false;
            active_targets.for_each_index([&](std::size_t target_pos) {
                if (batch_targets[target_pos].destination.get()
                    == branch.trace.current_physical.id) {
                    matches = true;
                }
            });
            return matches;
        }

        [[nodiscard]] std::map<ZoneId, std::size_t> completion_target_position_by_destination(
            std::span<const SearchCompletionTarget> batch_targets
        ) {
            std::map<ZoneId, std::size_t> positions;
            for (std::size_t target_pos = 0; target_pos < batch_targets.size(); ++target_pos) {
                positions[batch_targets[target_pos].destination] = target_pos;
            }
            return positions;
        }

        [[nodiscard]] std::map<ZoneId, std::size_t> projection_slot_position_by_destination(
            std::span<const SearchProjectionSlot> batch_slots
        ) {
            std::map<ZoneId, std::size_t> positions;
            for (std::size_t slot_pos = 0; slot_pos < batch_slots.size(); ++slot_pos) {
                positions[batch_slots[slot_pos].destination] = slot_pos;
            }
            return positions;
        }

        [[nodiscard]] mathfp::Expected<IntervalId> complete_retention_interval(
              const SearchProjectionSlot& slot
            , const SearchCostContext&    search_cost
        ) {
            if (slot.interval.has_value()) {
                return *slot.interval;
            }
            if (search_cost.mode != SearchCostMode::BaseOnly) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("completion-target projection currently requires base search cost")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
            return IntervalId{ 0 };
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_od_day_post_layer_retention(
              const SearchProjectionSlot&       slot
            , const SearchProjectionRetention&  retention
        ) {
            if (slot.kind != SearchProjectionSlotKind::OdDayPair) {
                return mathfp::kUnit;
            }
            if (!retention.complete_connections.alternatives.empty()
                || !retention.compact_complete_connections.metrics.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day post-layer retained raw complete alternatives")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                        .ctx(
                              "raw_complete"
                            , static_cast<std::int64_t>(
                                  retention.complete_connections.alternatives.size()
                              )
                          )
                        .ctx(
                              "compact_complete"
                            , static_cast<std::int64_t>(
                                  retention.compact_complete_connections.metrics.size()
                              )
                          )
                );
            }
            for (const auto& [signature, alternative] : retention.day_paths.alternatives_by_signature) {
                if (signature.origin != slot.origin
                    || signature.destination != slot.destination) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day post-layer retained path outside projection slot")
                            .ctx("slot_origin", slot.origin.get())
                            .ctx("slot_destination", slot.destination.get())
                            .ctx("path_origin", signature.origin.get())
                            .ctx("path_destination", signature.destination.get())
                    );
                }
                if (alternative.identity.signature != signature) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day post-layer path key and alternative identity disagree")
                            .ctx("origin", slot.origin.get())
                            .ctx("destination", slot.destination.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_od_day_post_layer_result(
              const SearchSlotResult& result
        ) {
            if (result.slot.kind != SearchProjectionSlotKind::OdDayPair) {
                return mathfp::kUnit;
            }
            if (!result.connections.empty()
                || result.connection_count != result.day_path_alternatives.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production slot result must contain only DayPath alternatives")
                        .ctx("origin", result.slot.origin.get())
                        .ctx("destination", result.slot.destination.get())
                        .ctx("connections", static_cast<std::int64_t>(result.connections.size()))
                        .ctx(
                              "day_paths"
                            , static_cast<std::int64_t>(result.day_path_alternatives.size())
                          )
                );
            }
            for (std::size_t i = 0; i < result.day_path_alternatives.size(); ++i) {
                const auto& alternative = result.day_path_alternatives[i];
                const auto& signature = alternative.identity.signature;
                if (signature.origin != result.slot.origin
                    || signature.destination != result.slot.destination) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day result path outside projection slot")
                            .ctx("slot_origin", result.slot.origin.get())
                            .ctx("slot_destination", result.slot.destination.get())
                            .ctx("path_origin", signature.origin.get())
                            .ctx("path_destination", signature.destination.get())
                            .ctx("alternative", static_cast<std::int64_t>(i))
                    );
                }
                if (alternative.support.split_support.supports.empty()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day result path has no timed split support")
                            .ctx("origin", result.slot.origin.get())
                            .ctx("destination", result.slot.destination.get())
                            .ctx("alternative", static_cast<std::int64_t>(i))
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> retain_timed_witness_as_day_path(
              SearchConnection            timed_witness
            , DayPathSignature            finalized_signature
            , const SearchProjectionSlot& slot
            , const SearchCostContext&    search_cost
            , SearchProjectionRetention&  retention
            , TaskSearchStats&            stats
        ) {
            /*
             * Production OD-day retention is final at the day-path level: a
             * completed timed witness has only statement-local lifetime here.
             * It is folded into the structural DayPath bucket immediately and
             * is never retained as a raw complete alternative of the search
             * slot.
             */
            const auto materialized_signature = day_path_signature_of(timed_witness);
            if (finalized_signature != materialized_signature) {
                return mathfp::unexpected(
                    mathfp::internal_error("incremental OD-day path prefix disagrees with materialized connection")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
            if (finalized_signature.origin != slot.origin
                || finalized_signature.destination != slot.destination) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day post-layer path signature does not match projection slot")
                        .ctx("slot_origin", slot.origin.get())
                        .ctx("slot_destination", slot.destination.get())
                        .ctx("path_origin", finalized_signature.origin.get())
                        .ctx("path_destination", finalized_signature.destination.get())
                );
            }

            MATHFP_TRY_LET(
                  IntervalId
                , interval
                , complete_retention_interval(slot, search_cost)
            );
            MATHFP_TRY_LET(
                  DayPathRetentionDecision
                , decision
                , retain_day_path_alternative(
                  retention.day_paths
                , std::move(finalized_signature)
                , std::move(timed_witness)
                , search_cost
                , interval
                , DayPathRetentionConfig{}
                )
            );
            ++stats.post_layer_day_path_candidates;
            if (decision.inserted_path) {
                ++stats.post_layer_day_path_inserted;
            }
            if (decision.replaced_representative) {
                ++stats.post_layer_day_path_representative_replaced;
            }
            stats.post_layer_day_path_supports = std::max(
                  stats.post_layer_day_path_supports
                , decision.timed_connection_count
            );
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> retain_od_day_completed_branch_as_day_path(
              const SearchBranch&        branch
            , const BranchArena&         branches
            , const PreprocessedNetwork& network
            , const SearchParams&        params
            , const SearchCostContext&   search_cost
            , const SearchProjectionSlot& slot
            , SearchProjectionRetention& retention
            , TaskSearchStats&           stats
        ) {
            if (!complete_branch_can_finish(
                  branch
                , network
                , params.transfers
                , slot.destination
            )) {
                return mathfp::kUnit;
            }

            ++stats.completed_connections;
            auto finalized_signature = make_day_path_signature_from_tree_label(
                  materialize_day_path_prefix(branch.od_day_carrier.path_identity)
                , slot.destination
            );
            MATHFP_TRY_LET(
                  std::optional<SearchConnection>
                , timed_witness
                , complete_connection(
                      branches
                    , branch
                    , network
                    , params.transfers
                    , slot.destination
                  )
            );
            if (!timed_witness.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day completed branch cannot be materialized as timed witness")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
            return retain_timed_witness_as_day_path(
                  std::move(*timed_witness)
                , std::move(finalized_signature)
                , slot
                , search_cost
                , retention
                , stats
            );
        }

        mathfp::Expected<mathfp::Unit> retain_complete_for_slot(
              const SearchBranch&        branch
            , const BranchArena&         branches
            , const PreprocessedNetwork& network
            , const SearchParams&        params
            , const SearchCostContext&   search_cost
            , const SearchProjectionSlot& slot
            , const AssignmentPeriodConfig& assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
            , const CompleteConnectionDominanceConfig& dominance_config
            , SearchProjectionRetention& retention
            , TaskSearchStats&           stats
        ) {
            if (slot.kind == SearchProjectionSlotKind::CompletionTarget) {
                if (!complete_branch_can_finish(
                      branch
                    , network
                    , params.transfers
                    , slot.destination
                )) {
                    return mathfp::kUnit;
                }

                ++stats.completed_connections;
                MATHFP_TRY_LET(
                      CompleteConnectionMetrics
                    , metrics
                    , complete_connection_metrics_from_branch(
                          branch
                        , search_cost
                      )
                );
                const auto retention_decision = retain_exact_compact_complete_connection(
                      retention.compact_complete_connections
                    , branches
                    , branch
                    , metrics
                    , dominance_config
                );
                if (!retention_decision) {
                    return mathfp::unexpected(std::move(retention_decision.error()));
                }
                stats.removed_complete_dominated += retention_decision->removed_dominated;
                if (!retention_decision->accepted) {
                    ++stats.rejected_complete_dominance;
                }
                return mathfp::kUnit;
            }

            if (slot.kind == SearchProjectionSlotKind::OdDayPair) {
                return retain_od_day_completed_branch_as_day_path(
                      branch
                    , branches
                    , network
                    , params
                    , search_cost
                    , slot
                    , retention
                    , stats
                );
            }

            MATHFP_TRY_LET(
                  std::optional<SearchConnection>
                , complete
                , complete_connection(
                      branches
                    , branch
                    , network
                    , params.transfers
                    , slot.destination
                  )
            );
            if (complete.has_value()) {
                ++stats.completed_connections;
                if (slot.kind == SearchProjectionSlotKind::DemandTask) {
                    const auto* task = slot.task;
                    if (task == nullptr) {
                        return mathfp::unexpected(
                            mathfp::internal_error("demand projection slot has no task while retaining complete connection")
                        );
                    }
                    const auto connection_metrics = metrics_of(*complete);
                    if (!connection_admissible_for_demand_segment(
                          connection_metrics
                        , task->interval
                        , assignment_period
                        , admissibility_config
                    )) {
                        ++stats.rejected_complete_admissibility;
                        return mathfp::kUnit;
                    }
                }
                MATHFP_TRY_LET(
                      IntervalId
                    , interval
                    , complete_retention_interval(slot, search_cost)
                );
                const auto retention_decision = retain_exact_complete_connection(
                      retention.complete_connections
                    , std::move(*complete)
                    , search_cost
                    , interval
                    , dominance_config
                );
                if (!retention_decision) {
                    return mathfp::unexpected(std::move(retention_decision.error()));
                }
                stats.removed_complete_dominated += retention_decision->removed_dominated;
                if (!retention_decision->accepted) {
                    ++stats.rejected_complete_dominance;
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<std::vector<SearchSlotResult>> search_batch_connections(
              const SearchBatch&                 batch
            , const PreprocessedNetwork&        network
            , const ResidualReverseGraph&        reverse_graph
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const ChoiceConfig&                choice_config
            , const AssignmentPeriodConfig&      assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
            , const SearchPruningExecutionPlan& pruning_execution
            , const CompleteConnectionDominanceConfig& complete_connection_dominance
            , SearchPartialRetentionScope partial_retention_scope
            , SearchDiagnosticsContext          diagnostics
            , std::size_t                       batch_index
            , std::size_t                       batch_count
            , const DayLevelSupplySearchGraph*  day_level_supply = nullptr
            , OdDayLabelRetentionConfig         od_day_label_retention_config =
                  OdDayLabelRetentionConfig{}
            , const SearchCancellationToken*    cancellation = nullptr
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            if (batch.completion_targets.empty()) {
                log(
                    fmt::format(
                          "search batch skipped: origin={} interval={} has no completion targets"
                        , batch.key.origin.get()
                        , format_batch_interval(batch.key.interval)
                    )
                    , LogLevel::Info
                );
                return std::vector<SearchSlotResult>{};
            }
            if (batch.projection_slots.empty()) {
                log(
                    fmt::format(
                          "search batch skipped: origin={} interval={} has no projection tasks"
                        , batch.key.origin.get()
                        , format_batch_interval(batch.key.interval)
                    )
                    , LogLevel::Info
                );
                return std::vector<SearchSlotResult>{};
            }
            MATHFP_TRY(validate_od_day_production_batch_runtime_contract(
                  batch
                , partial_retention_scope
                , pruning_execution
                , day_level_supply
            ));

            std::vector<SearchProjectionRetention> retentions;
            retentions.reserve(batch.projection_slots.size());
            for (const auto& slot : batch.projection_slots) {
                retentions.push_back(SearchProjectionRetention{ .slot = slot });
            }
            TreePartialRetention tree_partial_retention{};
            PaperConnectionLabelRegistry paper_label_registry{};

            TaskSearchStats                   stats;
            std::vector<TaskSearchStats>      task_stats(batch.projection_slots.size());
            BranchArena                       branches;
            std::deque<std::optional<FixedActiveMask>> completion_projection_states;
            std::deque<std::optional<DemandBranchProjectionState>> demand_projection_states;
            std::size_t                       released_branches = 0;
            const SearchTimeDomain*           first_departure_domain = batch.departure_domain;
            const auto                        batch_task_span =
                std::span<const SearchProjectionSlot>{
                      batch.projection_slots.data()
                    , batch.projection_slots.size()
                };
            const auto                        batch_target_span =
                std::span<const SearchCompletionTarget>{
                      batch.completion_targets.data()
                    , batch.completion_targets.size()
                };
            const auto target_projection_slots =
                completion_target_projection_slots(batch_task_span);
            const auto od_day_slots =
                od_day_projection_slots(batch_task_span);
            const auto* od_day_supply = od_day_slots ? nullptr : day_level_supply;
            const auto target_positions_by_destination = target_projection_slots
                ? completion_target_position_by_destination(batch_target_span)
                : std::map<ZoneId, std::size_t>{};
            const auto od_day_slot_positions_by_destination = od_day_slots
                ? projection_slot_position_by_destination(batch_task_span)
                : std::map<ZoneId, std::size_t>{};
            std::unordered_set<std::int64_t> od_day_destination_ids;
            if (od_day_slots) {
                od_day_destination_ids.reserve(batch.completion_targets.size());
                for (const auto& target : batch.completion_targets) {
                    od_day_destination_ids.insert(target.destination.get());
                }
            }
            if (target_projection_slots
                && batch.projection_slots.size() > FixedActiveMask::max_size) {
                return mathfp::unexpected(
                    mathfp::internal_error("completion-target fixed active mask capacity exceeded")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                        .ctx("mask_capacity", static_cast<std::int64_t>(FixedActiveMask::max_size))
                );
            }
            if (target_projection_slots) {
                if (batch.projection_slots.size() != batch.completion_targets.size()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("completion-target projection size disagrees with target count")
                            .ctx("origin", batch.key.origin.get())
                            .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                            .ctx("completion_targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                    );
                }
                for (std::size_t target_pos = 0; target_pos < batch.projection_slots.size(); ++target_pos) {
                    const auto& slot = batch.projection_slots[target_pos];
                    const auto& target = batch.completion_targets[target_pos];
                    if (!slot.completion_target.has_value()
                        || slot.completion_target->get() != static_cast<std::int64_t>(target_pos)
                        || slot.destination != target.destination) {
                        return mathfp::unexpected(
                            mathfp::internal_error("completion-target projection slot cannot share reachability mask with target")
                                .ctx("origin", batch.key.origin.get())
                                .ctx("position", static_cast<std::int64_t>(target_pos))
                                .ctx("slot_destination", slot.destination.get())
                                .ctx("target_destination", target.destination.get())
                        );
                    }
                }
            }
            std::optional<ResidualReachability> reachability;
            std::optional<ReachabilityMaskCache> reachability_cache;
            if (!od_day_slots) {
                reachability.emplace(build_residual_reachability(
                      reverse_graph
                    , batch_target_span
                    , params.transfers.max_transfers
                    , search_cost.impedance
                    , search_cost.fare_scale
                ));
                MATHFP_TRY(validate_residual_reachability(
                      *reachability
                    , params.transfers.max_transfers
                ));
                reachability_cache.emplace(
                    ReachabilityMaskCache{
                          .reachability   = *reachability
                        , .targets        = batch_target_span
                        , .slots          = batch_task_span
                        , .max_transfers  = params.transfers.max_transfers
                        , .unified_completion_targets = target_projection_slots
                    }
                );
            }

            LevelFrontierBuffer current_frontier;
            LevelFrontierBuffer next_frontier;
            //tex:
            // The tree is traversed with two frontier buffers. `current_frontier`
            // is exhausted before `next_frontier` becomes current. In this
            // implementation the level is transfer-depth oriented: walk legs and
            // the first boarding stay in the current level, while a later timed
            // boarding after a transfer moves the branch to the next level.
            const auto root_branch_index = append_branch(
                branches
              , SearchBranch{
                    .trace = SearchPartialTrace{
                          .origin                   = batch.key.origin
                        , .current_physical         = endpoint_key(batch.key.origin)
                        , .current_occurrence       = std::nullopt
                        , .phase                    = SearchBranchPhase::AtOrigin
                        , .parent_branch            = std::nullopt
                        , .incoming_segment         = std::nullopt
                        , .last_timed_segment       = nullptr
                        , .last_timed_route_segment = nullptr
                    }
                  , .metrics = SearchPartialMetrics{
                          .departure        = std::nullopt
                        , .current_time     = std::nullopt
                        , .access_time      = Time{ 0.0 }
                        , .in_vehicle_time  = Time{ 0.0 }
                        , .transfer_wait_time = Time{ 0.0 }
                        , .transfer_walk_time = Time{ 0.0 }
                        , .egress_time      = Time{ 0.0 }
                        , .transfers        = TransferCount{ 0 }
                        , .fare             = 0.0
                        , .capacity_exposure = CapacityExposure{ Time{ 0.0 } }
                  }
                  , .od_day_carrier = OdDayProductionCarrier{
                        .path_identity = make_od_day_path_prefix(batch.key.origin)
                    }
              }
            );
            if (diagnostics.validate_phase_invariants) {
                MATHFP_TRY(validate_search_branch_phase_invariants(
                    branch_at(branches, root_branch_index)
                ));
            }
            if (!od_day_slots && target_projection_slots) {
                const auto root_reachability_key = reachability_mask_key(
                      branch_at(branches, root_branch_index)
                    , params.transfers
                );
                auto root_reachable_targets = filter_target_positions_by_reachability(
                      ActiveIndexSet::full(batch.completion_targets.size())
                    , reachability_cache->target_entry(root_reachability_key)
                );
                auto root_reachability = filter_task_positions_by_reachability(
                      ActiveIndexSet::full(batch.projection_slots.size())
                    , reachability_cache->slot_entry(root_reachability_key)
                );
                record_reachability_rejections(
                      std::span<const RejectedReachabilityTask>{
                          root_reachability.unreachable.data()
                        , root_reachability.unreachable.size()
                      }
                    , task_stats
                    , stats
                );
                if (!root_reachability.reachable.equals(root_reachable_targets)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("completion-target root reachability masks disagree")
                            .ctx("origin", batch.key.origin.get())
                    );
                }
                completion_projection_states.push_back(
                    FixedActiveMask::from(root_reachability.reachable)
                );
            } else if (!od_day_slots) {
                const auto root_reachability_key = reachability_mask_key(
                      branch_at(branches, root_branch_index)
                    , params.transfers
                );
                auto root_reachable_targets = filter_target_positions_by_reachability(
                      ActiveIndexSet::full(batch.completion_targets.size())
                    , reachability_cache->target_entry(root_reachability_key)
                );
                auto root_reachability = filter_task_positions_by_reachability(
                      ActiveIndexSet::full(batch.projection_slots.size())
                    , reachability_cache->slot_entry(root_reachability_key)
                );
                record_reachability_rejections(
                      std::span<const RejectedReachabilityTask>{
                          root_reachability.unreachable.data()
                        , root_reachability.unreachable.size()
                      }
                    , task_stats
                    , stats
                );
                demand_projection_states.push_back(
                    DemandBranchProjectionState{
                          .active_tasks = std::move(root_reachability.reachable)
                        , .active_targets = std::move(root_reachable_targets)
                    }
                );
            }
            current_frontier.push_back(root_branch_index);
            BranchPhaseStats current_frontier_by_phase;
            BranchPhaseStats next_frontier_by_phase;
            increment_phase_stats(
                  current_frontier_by_phase
                , SearchBranchPhase::AtOrigin
            );
            auto retained_production_alternative_count = [&]() noexcept {
                return od_day_slots
                    ? retained_day_path_count(retentions)
                    : retained_connection_count(retentions);
            };

            status(
                fmt::format(
                      "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} frontier={} found={}"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                    , batch.projection_slots.size()
                    , batch.completion_targets.size()
                    , diagnostics.capacity_iteration
                    , current_frontier.size()
                    , retained_production_alternative_count()
                )
            );

            using Clock = std::chrono::steady_clock;
            const auto batch_started_at = Clock::now();
            auto last_wall_clock_heartbeat = batch_started_at;
            auto projection_state_count = [&]() noexcept {
                if (od_day_slots) {
                    return std::size_t{ 0u };
                }
                return target_projection_slots
                    ? completion_projection_states.size() - released_branches
                    : demand_projection_states.size() - released_branches;
            };
            const auto projection_state_size = od_day_slots
                ? std::size_t{ 0u }
                : (target_projection_slots
                    ? sizeof(FixedActiveMask)
                    : sizeof(DemandBranchProjectionState));
            const OdDayProductionMemoryLimits od_day_memory_limits{};
            auto make_storage_diagnostics = [&]() noexcept {
                return search_storage_diagnostics(
                      branches
                    , released_branches
                    , projection_state_count()
                    , projection_state_size
                    , tree_partial_retention
                    , std::span<const SearchProjectionRetention>{
                          retentions.data()
                        , retentions.size()
                      }
                    , stats.pruning
                );
            };
            auto emit_storage_diagnostics = [&]() {
                const auto storage_diagnostics = make_storage_diagnostics();
                log(format_search_storage_diagnostics(storage_diagnostics), LogLevel::Info);
                if (od_day_slots) {
                    log(
                        format_od_day_theory_diagnostics(
                              storage_diagnostics
                            , stats
                            , current_frontier.size()
                            , next_frontier.size()
                            , od_day_label_retention_config
                        )
                        , LogLevel::Info
                    );
                }
            };
            if (od_day_slots) {
                log(
                    fmt::format(
                          "OD-day production memory limits: carrier=compact_connection_segment_prefix branch_slots={} live_branches={} frontier={} legacy_od_day_label_states={} label_representatives_per_state={} post_layer_day_paths={} approx_direct_mb={}"
                        , format_optional_size_limit(od_day_memory_limits.max_branch_slots_per_tree)
                        , format_optional_size_limit(od_day_memory_limits.max_live_branches_per_tree)
                        , format_optional_size_limit(od_day_memory_limits.max_frontier_per_tree)
                        , format_optional_size_limit(od_day_memory_limits.max_od_day_label_states_per_tree)
                        , od_day_label_retention_config.max_representatives_per_label
                        , format_optional_size_limit(od_day_memory_limits.max_retained_day_paths_per_tree)
                        , format_optional_mb_limit(od_day_memory_limits.max_approximate_direct_bytes_per_tree)
                    )
                    , LogLevel::Info
                );
            }
            auto emit_wall_clock_heartbeat =
                [&](const char* stage, std::size_t branch_index) {
                    const auto now = Clock::now();
                    if (now - last_wall_clock_heartbeat
                        < kSearchWallClockHeartbeatInterval) {
                        return;
                    }
                    last_wall_clock_heartbeat = now;
                    const auto elapsed_ms = std::chrono::duration_cast<
                        std::chrono::milliseconds
                    >(now - batch_started_at).count();
                    log(
                        fmt::format(
                              "search wall heartbeat: batch={:>8} origin={:>4} interval={:>4}"
                              " tasks={:>5} targets={:>5} capacity_iteration={:>4} stage={} branch={} elapsed_ms={}"
                              " expanded={:>8} generated={:>8} accepted={:>8} found={:>8}"
                              " frontier={}/{}"
                              " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={})"
                              " frontier_phase(current={}, next={})"
                              " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                              " accepted_phase({})"
                            , static_cast<std::int64_t>(batch_index)
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , batch.projection_slots.size()
                            , batch.completion_targets.size()
                            , diagnostics.capacity_iteration
                            , stage
                            , static_cast<std::int64_t>(branch_index)
                            , static_cast<std::int64_t>(elapsed_ms)
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , retained_production_alternative_count()
                            , current_frontier.size()
                            , next_frontier.size()
                            , stats.stale_frontier_skipped
                            , stats.c_y_removed_dominated
                            , stats.c_y_removed_stale
                            , stats.frontier_compaction_runs
                            , stats.frontier_compaction_removed
                            , format_branch_phase_stats(current_frontier_by_phase)
                            , format_branch_phase_stats(next_frontier_by_phase)
                            , format_walk_lookup_stats(stats.walk_lookup)
                            , format_walk_kind_stats(stats.generated_walk)
                            , format_walk_kind_stats(stats.accepted_walk)
                            , stats.rejected_consecutive_walk
                            , format_branch_phase_stats(stats.accepted_branches_by_phase)
                        )
                        , LogLevel::Info
                    );
                    emit_storage_diagnostics();
                };
            auto release_projection_payload = [&](std::size_t released_index) noexcept {
                if (od_day_slots) {
                    ++released_branches;
                    return;
                }
                if (target_projection_slots) {
                    if (released_index < completion_projection_states.size()) {
                        completion_projection_states[released_index].reset();
                    }
                } else {
                    if (released_index < demand_projection_states.size()) {
                        demand_projection_states[released_index].reset();
                    }
                }
                ++released_branches;
            };
            constexpr auto kOdDayFrontierCompactionMinRemoved = std::size_t{ 1024u };
            constexpr auto kOdDayFrontierCompactionMinSize    = std::size_t{ 4096u };
            auto c_y_removed_since_frontier_compaction = std::size_t{ 0u };
            auto compact_frontier_queue =
                [&](LevelFrontierBuffer& queue, BranchPhaseStats& phase_stats) {
                    auto kept = std::vector<std::size_t>{};
                    kept.reserve(queue.size());
                    auto removed = std::size_t{ 0u };
                    for (auto i = queue.head; i < queue.entries.size(); ++i) {
                        const auto queued_index = queue.entries[i];
                        if (!branch_alive(branches, queued_index)) {
                            ++removed;
                            continue;
                        }
                        const auto& queued_branch = branch_at(branches, queued_index);
                        if (paper_connection_label_active(
                              paper_label_registry
                            , queued_branch.paper_connection_label
                        )) {
                            kept.push_back(queued_index);
                            continue;
                        }
                        decrement_phase_stats(phase_stats, queued_branch.trace.phase);
                        release_branch_if_closed(
                              branches
                            , queued_index
                            , release_projection_payload
                        );
                        ++removed;
                    }
                    queue.entries = std::move(kept);
                    queue.head = 0u;
                    return removed;
                };
            auto compact_od_day_frontiers = [&](const char* reason) {
                if (!od_day_slots) {
                    return;
                }
                const auto removed_current = compact_frontier_queue(
                      current_frontier
                    , current_frontier_by_phase
                );
                const auto removed_next = compact_frontier_queue(
                      next_frontier
                    , next_frontier_by_phase
                );
                const auto removed = removed_current + removed_next;
                if (removed == 0u) {
                    return;
                }
                ++stats.frontier_compaction_runs;
                stats.frontier_compaction_removed += removed;
                stats.frontier_compaction_removed_current += removed_current;
                stats.frontier_compaction_removed_next += removed_next;
                log(
                    fmt::format(
                          "OD-day frontier compacted: origin={} reason={} removed={} current_removed={} next_removed={} frontier={}/{}"
                        , batch.key.origin.get()
                        , reason
                        , removed
                        , removed_current
                        , removed_next
                        , current_frontier.size()
                        , next_frontier.size()
                    )
                    , LogLevel::Debug
                );
            };
            const auto paper_connection_tree_targets =
                od_day_slots
                    ? ActiveIndexSet::full(batch.completion_targets.size())
                    : ActiveIndexSet{};

            while (!current_frontier.empty() || !next_frontier.empty()) {
                if (search_cancelled(cancellation)) {
                    log(
                        fmt::format(
                              "search batch cancelled: {}/{} origin={} interval={} expanded={} accepted={} found={} reason=sibling_failed"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_production_alternative_count()
                        )
                        , LogLevel::Warning
                    );
                    return std::vector<SearchSlotResult>{};
                }
                if (current_frontier.empty()) {
                    compact_od_day_frontiers("level_swap");
                    c_y_removed_since_frontier_compaction = 0u;
                    current_frontier.swap(next_frontier);
                    current_frontier_by_phase = next_frontier_by_phase;
                    next_frontier_by_phase = BranchPhaseStats{};
                    if (current_frontier.empty()) {
                        continue;
                    }
                }

                stats.max_current_frontier = std::max(stats.max_current_frontier, current_frontier.size());
                stats.max_next_frontier    = std::max(stats.max_next_frontier   , next_frontier   .size());

                const auto branch_index = current_frontier.front();
                current_frontier.pop_front();
                const auto& branch = branch_at(branches, branch_index);
                decrement_phase_stats(current_frontier_by_phase, branch.trace.phase);
                if (od_day_slots
                    && !paper_connection_label_active(
                          paper_label_registry
                        , branch.paper_connection_label
                    )) {
                    ++stats.stale_frontier_skipped;
                    release_branch_if_closed(
                          branches
                        , branch_index
                        , release_projection_payload
                    );
                    continue;
                }
                emit_wall_clock_heartbeat("branch", branch_index);
                ActiveIndexSet completion_active;
                const ActiveIndexSet* active_tasks_ptr{};
                const ActiveIndexSet* active_targets_ptr{};
                if (od_day_slots) {
                    active_tasks_ptr   = &paper_connection_tree_targets;
                    active_targets_ptr = &paper_connection_tree_targets;
                } else if (target_projection_slots) {
                    completion_active =
                        completion_projection_states[branch_index]->to_active_index_set();
                    active_tasks_ptr   = &completion_active;
                    active_targets_ptr = &completion_active;
                } else {
                    const auto& projection_state = *demand_projection_states[branch_index];
                    active_tasks_ptr   = &projection_state.active_tasks;
                    active_targets_ptr = &projection_state.active_targets;
                }
                const auto& active_tasks   = *active_tasks_ptr;
                const auto& active_targets = *active_targets_ptr;
                const auto active_destinations = od_day_slots
                    ? ActiveDestinationMembership{
                          .direct_destination_ids = &od_day_destination_ids
                      }
                    : ActiveDestinationMembership{
                          .active_targets = &active_targets
                        , .batch_targets  = batch_target_span
                      };
                ++stats.expanded_branches;
                if (od_day_slots && ((stats.expanded_branches % 1024u) == 0u)) {
                    MATHFP_TRY(validate_od_day_production_memory_limits(
                          make_storage_diagnostics()
                        , current_frontier.size()
                        , next_frontier.size()
                        , od_day_memory_limits
                        , batch.key.origin
                    ));
                }

                if ((stats.expanded_branches % kSearchHeartbeatStep) == 0) {
                    status(
                        fmt::format(
                              "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} expanded={} accepted={} found={} frontier={}/{}"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , batch.projection_slots.size()
                            , batch.completion_targets.size()
                            , diagnostics.capacity_iteration
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_production_alternative_count()
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                    );
                    log(
                        fmt::format(
                              "search heartbeat: batch={:>8} origin={:>4} interval={:>4} tasks={:>5} targets={:>5} capacity_iteration={:>4} expanded={:>8} generated={:>8}"
                              " accepted={:>8} found={:>8} rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                              " lower_bound_pruned={}"
                              " pruning(exact/approx/inserted/skipped)={}/{}/{}/{}"
                              " frontier={}/{}"
                              " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={})"
                              " paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{}"
                              " late_guard(time_domain/feasibility/reboarding)={}/{}/{}"
                              " frontier_phase(current={}, next={})"
                              " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                              " accepted_phase({})"
                            , static_cast<std::int64_t>(batch_index)
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , batch.projection_slots.size()
                            , batch.completion_targets.size()
                            , diagnostics.capacity_iteration
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , retained_production_alternative_count()
                            , stats.rejected_time_domain
                            , stats.rejected_feasibility
                            , stats.rejected_reboarding
                            , stats.rejected_cycles
                            , stats.rejected_transfer_limit
                            , stats.rejected_reachability
                            , stats.rejected_dominance_or_tolerance
                            , stats.rejected_suffix_lower_bound
                            , stats.pruning.rejected_exact
                            , stats.pruning.rejected_approximate
                            , stats.pruning.inserted_metrics
                            , stats.pruning.skipped_insertions
                            , current_frontier.size()
                            , next_frontier   .size()
                            , stats.stale_frontier_skipped
                            , stats.c_y_removed_dominated
                            , stats.c_y_removed_stale
                            , stats.frontier_compaction_runs
                            , stats.frontier_compaction_removed
                            , stats.paper_timed_lookup_skipped_phase
                            , stats.paper_timed_lookup_skipped_transfer_budget
                            , stats.paper_timed_successor_rejected_time_domain
                            , stats.paper_timed_successor_rejected_same_trip
                            , stats.paper_timed_successor_rejected_same_line
                            , stats.paper_timed_successor_rejected_feasibility
                            , stats.rejected_time_domain
                            , stats.rejected_feasibility
                            , stats.rejected_reboarding
                            , format_branch_phase_stats(current_frontier_by_phase)
                            , format_branch_phase_stats(next_frontier_by_phase)
                            , format_walk_lookup_stats(stats.walk_lookup)
                            , format_walk_kind_stats(stats.generated_walk)
                            , format_walk_kind_stats(stats.accepted_walk)
                            , stats.rejected_consecutive_walk
                            , format_branch_phase_stats(stats.accepted_branches_by_phase)
                        )
                        , LogLevel::Info
                    );
                    log(
                        format_search_pruning_runtime_stats(summarize(stats.pruning))
                        , LogLevel::Info
                    );
                    emit_storage_diagnostics();
                }

                if (active_targets.empty()
                    || (branch.trace.current_physical.kind == EndpointKind::Zone
                        && branch.metrics.departure.has_value())) {
                    release_branch_if_closed(
                          branches
                        , branch_index
                        , release_projection_payload
                    );
                    continue;
                }

                mathfp::Expected<mathfp::Unit> successor_error = mathfp::kUnit;
                bool batch_cancelled = false;
                for_each_successor(
                      network
                    , od_day_supply
                    , batch.key.origin
                    , active_destinations
                    , branch
                    , params.transfers
                    , first_departure_domain
                    , &stats
                    , [&](const SearchSuccessor& successor_ref) {
                    if (search_cancelled(cancellation)) {
                        batch_cancelled = true;
                        return;
                    }
                    if (!successor_error) {
                        return;
                    }
                    ++stats.generated_successors;
                    if (successor_ref.walk_transition.has_value()) {
                        increment_walk_kind_stats(stats.generated_walk, successor_ref.walk_transition->kind);
                    }
                    if ((stats.generated_successors % kSearchWallClockSuccessorCheckStep) == 0) {
                        emit_wall_clock_heartbeat("successor", branch_index);
                    }
                    const auto& successor = connection_segment_at(network, successor_ref.connection);
                    if (!od_day_slots || !successor_ref.support_envelope.has_value()) {
                        /*
                         * In production OD-day this is an invariant guard: the
                         * generator must have emitted only insertable paper
                         * successors. Legacy timed search keeps the predicate
                         * as its working filter.
                         */
                        if (!paper_search_successor_feasible_before_branch(
                              branch
                            , network
                            , successor_ref
                            , first_departure_domain
                            , params.transfers
                            , stats
                        )) {
                            if (od_day_slots) {
                                successor_error = mathfp::unexpected(
                                    mathfp::internal_error("OD-day paper successor failed late insertability invariant")
                                        .ctx("origin", batch.key.origin.get())
                                        .ctx("branch", static_cast<std::int64_t>(branch_index))
                                        .ctx("connection", static_cast<std::int64_t>(successor_ref.connection.get()))
                                );
                            }
                            return;
                        }
                    }

                    std::optional<PaperConnectionLabelId> accepted_paper_label;
                    if (od_day_slots
                        && partial_retention_scope
                            == SearchPartialRetentionScope::TreeGlobal) {
                        bool rejected_cycle = false;
                        auto paper_prefix =
                            evaluate_paper_connection_prefix_before_branch(
                                  branches
                                , branch
                                , network
                                , successor_ref
                                , batch.key.interval
                                , search_cost
                                , rejected_cycle
                            );
                        if (!paper_prefix) {
                            successor_error = mathfp::unexpected(
                                std::move(paper_prefix.error())
                            );
                            return;
                        }
                        if (!paper_prefix->connection_candidate.has_value()) {
                            if (rejected_cycle) {
                                ++stats.rejected_cycles;
                                return;
                            }
                        } else {
                            if (paper_prefix->connection_candidate->metrics.transfers
                                > params.transfers.max_transfers) {
                                ++stats.rejected_transfer_limit;
                                return;
                            }

                            auto pruning_decision = retain_paper_connection_tree_node(
                                  paper_prefix->connection_candidate->node
                                , std::move(paper_prefix->connection_candidate->metrics)
                                , branch.paper_connection_label
                                , paper_label_registry
                                , tree_partial_retention
                                , params
                                , pruning_execution
                                , stats.pruning
                            );
                            if (!pruning_decision) {
                                successor_error = mathfp::unexpected(
                                    std::move(pruning_decision.error())
                                );
                                return;
                            }
                            if (!pruning_decision->accepted()) {
                                ++stats.rejected_dominance_or_tolerance;
                                return;
                            }
                            for (const auto removed_label : pruning_decision->removed_labels) {
                                deactivate_paper_connection_label(
                                      paper_label_registry
                                    , removed_label
                                );
                            }
                            const auto removed_from_c_y =
                                  pruning_decision->removed_labels.size()
                                + pruning_decision->removed_stale_labels;
                            stats.c_y_removed_dominated +=
                                pruning_decision->removed_labels.size();
                            stats.c_y_removed_stale +=
                                pruning_decision->removed_stale_labels;
                            c_y_removed_since_frontier_compaction += removed_from_c_y;
                            if (c_y_removed_since_frontier_compaction
                                    >= kOdDayFrontierCompactionMinRemoved
                                && (current_frontier.size() + next_frontier.size())
                                    >= kOdDayFrontierCompactionMinSize) {
                                compact_od_day_frontiers("c_y_removal");
                                c_y_removed_since_frontier_compaction = 0u;
                            }
                            accepted_paper_label = pruning_decision->label;
                        }
                    }

                    auto candidate_result = extend_branch(
                          branches
                        , branch_index
                        , branch
                        , network
                        , successor_ref
                        , batch.key.interval
                        , search_cost
                    );
                    if (!candidate_result) {
                        successor_error = mathfp::unexpected(
                            std::move(candidate_result.error())
                        );
                        return;
                    }
                    auto candidate = std::move(*candidate_result);
                    if (!candidate.has_value()) {
                        ++stats.rejected_cycles;
                        return;
                    }
                    if (od_day_slots) {
                        /*
                         * Production OD-day candidates carry their structural
                         * path and timed witness in compact prefixes. Detaching
                         * the trace parent before any retention/materialization
                         * keeps the production contour from falling back to
                         * raw timed prefix chains.
                         */
                        candidate->trace.parent_branch = std::nullopt;
                        candidate->paper_connection_label = accepted_paper_label;
                    }
                    if (diagnostics.validate_phase_invariants) {
                        if (auto invariant_result = validate_search_branch_phase_invariants(*candidate);
                            !invariant_result) {
                            successor_error = mathfp::unexpected(
                                std::move(invariant_result.error())
                            );
                            return;
                        }
                    }

                    if (candidate->metrics.transfers > params.transfers.max_transfers) {
                        ++stats.rejected_transfer_limit;
                        return;
                    }

                    std::vector<std::size_t> complete_task_positions;
                    bool completed_target = false;
                    if (od_day_slots
                        && candidate->metrics.departure.has_value()
                        && candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        const auto position_it = od_day_slot_positions_by_destination.find(
                            ZoneId{ candidate->trace.current_physical.id }
                        );
                        if (position_it != od_day_slot_positions_by_destination.end()) {
                            completed_target = true;
                            complete_task_positions.push_back(position_it->second);
                        }
                    } else if (target_projection_slots
                        && candidate->metrics.departure.has_value()
                        && candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        const auto position_it = target_positions_by_destination.find(
                            ZoneId{ candidate->trace.current_physical.id }
                        );
                        if (position_it != target_positions_by_destination.end()
                            && active_targets.contains(position_it->second)) {
                            completed_target = true;
                            complete_task_positions.push_back(position_it->second);
                        }
                    } else {
                        completed_target = matches_completion_target(
                              *candidate
                            , active_targets
                            , batch_target_span
                        );
                        if (completed_target) {
                            complete_task_positions = matching_complete_tasks(
                                  *candidate
                                , active_tasks
                                , batch_task_span
                            );
                        }
                    }
                    if (completed_target) {
                        bool retained_complete = false;
                        for (const auto task_pos : complete_task_positions) {
                            const auto completed_before =
                                task_stats[task_pos].completed_connections;
                            const auto post_layer_candidates_before =
                                task_stats[task_pos].post_layer_day_path_candidates;
                            const auto post_layer_inserted_before =
                                task_stats[task_pos].post_layer_day_path_inserted;
                            const auto post_layer_replaced_before =
                                task_stats[task_pos].post_layer_day_path_representative_replaced;
                            auto complete_result = retain_complete_for_slot(
                                  *candidate
                                , branches
                                , network
                                , params
                                , search_cost
                                , batch.projection_slots[task_pos]
                                , assignment_period
                                , admissibility_config
                                , complete_connection_dominance
                                , retentions[task_pos]
                                , task_stats[task_pos]
                            );
                            if (!complete_result) {
                                successor_error = mathfp::unexpected(
                                    std::move(complete_result.error())
                                );
                                return;
                            }
                            stats.completed_connections +=
                                task_stats[task_pos].completed_connections - completed_before;
                            stats.post_layer_day_path_candidates +=
                                  task_stats[task_pos].post_layer_day_path_candidates
                                - post_layer_candidates_before;
                            stats.post_layer_day_path_inserted +=
                                  task_stats[task_pos].post_layer_day_path_inserted
                                - post_layer_inserted_before;
                            stats.post_layer_day_path_representative_replaced +=
                                  task_stats[task_pos].post_layer_day_path_representative_replaced
                                - post_layer_replaced_before;
                            stats.post_layer_day_path_supports = std::max(
                                  stats.post_layer_day_path_supports
                                , task_stats[task_pos].post_layer_day_path_supports
                            );
                            retained_complete =
                                retained_complete
                                || task_stats[task_pos].completed_connections > completed_before;
                        }
                        if (retained_complete && successor_ref.walk_transition.has_value()) {
                            increment_walk_kind_stats(
                                  stats.accepted_walk
                                , successor_ref.walk_transition->kind
                            );
                        }
                        return;
                    }

                    if (candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }

                    if (od_day_slots) {
                        ++stats.accepted_branches;
                        if (successor_ref.walk_transition.has_value()) {
                            increment_walk_kind_stats(
                                  stats.accepted_walk
                                , successor_ref.walk_transition->kind
                            );
                        }
                        increment_phase_stats(
                              stats.accepted_branches_by_phase
                            , candidate->trace.phase
                        );
                        const auto candidate_phase = candidate->trace.phase;
                        const auto candidate_index = append_branch(
                              branches
                            , std::move(*candidate)
                        );
                        next_frontier.push_back(candidate_index);
                        increment_phase_stats(next_frontier_by_phase, candidate_phase);
                        if (od_day_memory_limits.max_frontier_per_tree.has_value()
                            && (current_frontier.size() + next_frontier.size()
                                > *od_day_memory_limits.max_frontier_per_tree)) {
                            successor_error = validate_od_day_production_memory_limits(
                                  make_storage_diagnostics()
                                , current_frontier.size()
                                , next_frontier.size()
                                , od_day_memory_limits
                                , batch.key.origin
                            );
                        }
                        return;
                    }

                    const auto candidate_reachability_key = reachability_mask_key(
                          *candidate
                        , params.transfers
                    );
                    const auto& target_reachability_entry =
                        reachability_cache->target_entry(candidate_reachability_key);
                    auto next_active_targets = filter_target_positions_by_reachability(
                          active_targets
                        , target_reachability_entry
                    );
                    if (next_active_targets.empty()) {
                        add_reachability_rejection(
                              stats
                            , summarize_target_reachability_rejection(
                                  active_targets
                                , target_reachability_entry
                              )
                        );
                        return;
                    }

                    if (!od_day_slots
                        && partial_retention_scope
                            == SearchPartialRetentionScope::TreeGlobal) {
                        auto pruning_decision = retain_branch(
                                  *candidate
                                , tree_partial_retention
                                , params
                                , search_cost
                                , pruning_execution
                                , stats.pruning
                              );
                        if (!pruning_decision) {
                            successor_error = mathfp::unexpected(
                                std::move(pruning_decision.error())
                            );
                            return;
                        }
                        if (!pruning_decision->accepted) {
                            ++stats.rejected_dominance_or_tolerance;
                            return;
                        }
                    }

                    const auto reachable_tasks = filter_task_positions_by_reachability(
                          active_tasks
                        , reachability_cache->slot_entry(candidate_reachability_key)
                    );
                    record_reachability_rejections(
                          std::span<const RejectedReachabilityTask>{
                              reachable_tasks.unreachable.data()
                            , reachable_tasks.unreachable.size()
                          }
                        , task_stats
                        , stats
                    );

                    ActiveIndexSet next_active_tasks{ batch.projection_slots.size() };
                    std::vector<RejectedSuffixLowerBoundTask> lower_bound_rejected_tasks;
                    lower_bound_rejected_tasks.reserve(reachable_tasks.reachable.active_count());
                    reachable_tasks.reachable.for_each_index([&](std::size_t task_pos) {
                        if (!successor_error) {
                            return;
                        }
                        if (batch.projection_slots[task_pos].kind
                            == SearchProjectionSlotKind::OdDayPair) {
                            next_active_tasks.set(task_pos);
                            return;
                        }
                        auto lower_bound_decision = target_projection_slots
                            ? evaluate_suffix_lower_bound_pruning(
                                  *candidate
                                , batch.projection_slots[task_pos].destination
                                , *reachability
                                , retentions[task_pos].compact_complete_connections
                                , params
                                , search_cost
                                , choice_config
                                , complete_connection_dominance
                              )
                            : evaluate_suffix_lower_bound_pruning(
                                  *candidate
                                , batch.projection_slots[task_pos].destination
                                , *reachability
                                , retentions[task_pos].complete_connections
                                , params
                                , search_cost
                                , choice_config
                                , complete_connection_dominance
                              );
                        if (!lower_bound_decision) {
                            successor_error = mathfp::unexpected(
                                std::move(lower_bound_decision.error())
                            );
                            return;
                        }
                        if (!lower_bound_decision->feasible) {
                            lower_bound_rejected_tasks.push_back(
                                RejectedSuffixLowerBoundTask{
                                      .task_position = task_pos
                                    , .reason        = lower_bound_decision->rejection_reason
                                }
                            );
                            ++task_stats[task_pos].rejected_dominance_or_tolerance;
                            return;
                        }

                        if (partial_retention_scope
                            == SearchPartialRetentionScope::ProjectionSlotLocal) {
                            auto pruning_decision = retain_branch(
                                  *candidate
                                , retentions[task_pos]
                                , params
                                , search_cost
                                , pruning_execution
                                , stats.pruning
                            );
                            if (!pruning_decision) {
                                successor_error = mathfp::unexpected(
                                    std::move(pruning_decision.error())
                                );
                                return;
                            }
                            if (!pruning_decision->accepted) {
                                ++task_stats[task_pos].rejected_dominance_or_tolerance;
                                return;
                            }
                        }
                        next_active_tasks.set(task_pos);
                    });
                    if (!successor_error) {
                        return;
                    }
                    record_suffix_lower_bound_rejections(
                          std::span<const RejectedSuffixLowerBoundTask>{
                              lower_bound_rejected_tasks.data()
                            , lower_bound_rejected_tasks.size()
                          }
                        , task_stats
                        , stats
                    );
                    if (next_active_tasks.empty()) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }

                    ++stats.accepted_branches;
                    if (successor_ref.walk_transition.has_value()) {
                        increment_walk_kind_stats(
                              stats.accepted_walk
                            , successor_ref.walk_transition->kind
                        );
                    }
                    increment_phase_stats(
                          stats.accepted_branches_by_phase
                        , candidate->trace.phase
                    );
                    const auto same_level =
                        is_walk_connection(successor) || !branch.metrics.departure.has_value();
                    //tex:
                    // Frontier placement follows the transfer-depth level used
                    // above: always-available walk segments and the first timed
                    // boarding do not increase $$NT$$, while subsequent timed
                    // boardings represent the next transfer level.
                    const auto candidate_phase = candidate->trace.phase;
                    const auto candidate_index = append_branch(
                          branches
                        , std::move(*candidate)
                    );
                    if (target_projection_slots) {
                        completion_projection_states.push_back(
                            FixedActiveMask::from(next_active_tasks)
                        );
                    } else {
                        demand_projection_states.push_back(
                            DemandBranchProjectionState{
                                  .active_tasks = std::move(next_active_tasks)
                                , .active_targets = std::move(next_active_targets)
                            }
                        );
                    }
                    if (same_level) {
                        current_frontier.push_back(candidate_index);
                        increment_phase_stats(current_frontier_by_phase, candidate_phase);
                    } else {
                        next_frontier   .push_back(candidate_index);
                        increment_phase_stats(next_frontier_by_phase, candidate_phase);
                    }
                    if (od_day_slots
                        && od_day_memory_limits.max_frontier_per_tree.has_value()
                        && (current_frontier.size() + next_frontier.size()
                            > *od_day_memory_limits.max_frontier_per_tree)) {
                        successor_error = validate_od_day_production_memory_limits(
                              make_storage_diagnostics()
                            , current_frontier.size()
                            , next_frontier.size()
                            , od_day_memory_limits
                            , batch.key.origin
                        );
                    }
                }
                    , [&](std::size_t rejected_walk_count) {
                          stats.rejected_consecutive_walk += rejected_walk_count;
                      }
                );
                release_branch_if_closed(
                      branches
                    , branch_index
                    , release_projection_payload
                );
                if (batch_cancelled) {
                    log(
                        fmt::format(
                              "search batch cancelled during successor scan: {}/{} origin={} interval={} expanded={} accepted={} found={} reason=sibling_failed"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_production_alternative_count()
                        )
                        , LogLevel::Warning
                    );
                    return std::vector<SearchSlotResult>{};
                }
                MATHFP_TRY(std::move(successor_error));
            }

            if (od_day_slots) {
                stats.c_y_removed_stale += remove_inactive_paper_connection_metrics(
                      tree_partial_retention.paper_connections
                    , paper_label_registry
                );
                MATHFP_TRY(validate_paper_connection_label_sync(
                      tree_partial_retention.paper_connections
                    , paper_label_registry
                    , batch.key.origin
                ));
            }

            std::vector<SearchSlotResult> slot_results;
            slot_results.reserve(batch.projection_slots.size());
            std::size_t batch_final_found = 0;
            std::size_t batch_retained_before_tolerance = 0;
            for (std::size_t task_pos = 0; task_pos < batch.projection_slots.size(); ++task_pos) {
                const auto& slot = batch.projection_slots[task_pos];
                auto& retention   = retentions[task_pos];
                MATHFP_TRY(validate_od_day_post_layer_retention(slot, retention));
                const auto before_tolerance = target_projection_slots
                    ? retention.compact_complete_connections.metrics.size()
                    : slot.kind == SearchProjectionSlotKind::OdDayPair
                        ? day_path_retention_size(retention.day_paths)
                        : retention.complete_connections.alternatives.size();
                std::vector<SearchConnection> connections;
                std::vector<DayPathAlternative> day_path_alternatives;
                std::size_t connection_count = 0u;
                if (target_projection_slots) {
                    connection_count = finalize_compact_complete_connection_count(
                          retention.compact_complete_connections
                        , params.choice_tolerances
                        , choice_config.rollout_stage
                    );
                } else {
                    if (slot.kind == SearchProjectionSlotKind::OdDayPair) {
                        /*
                         * Production OD-day search finalizes only OD path
                         * alternatives. Raw completed SearchConnection objects
                         * remain support payload under DayPathAlternative and
                         * must not become the slot result.
                         */
                        (void)choice_config;
                        day_path_alternatives = finalize_day_path_alternatives(
                            std::move(retention.day_paths)
                        );
                        for (std::size_t i = 0; i < day_path_alternatives.size(); ++i) {
                            MATHFP_TRY(validate_day_path_alternative(
                                  day_path_alternatives[i]
                                , i
                            ));
                        }
                        connection_count = day_path_alternatives.size();
                    } else {
                        connections = finalize_complete_connection_retention(
                              retention.complete_connections
                            , params.choice_tolerances
                            , choice_config.rollout_stage
                        );
                        connection_count = connections.size();
                    }
                    task_stats[task_pos].rejected_complete_tolerance =
                        before_tolerance - connection_count;
                }
                if (target_projection_slots) {
                    task_stats[task_pos].rejected_complete_tolerance =
                        before_tolerance - connection_count;
                }
                stats.rejected_complete_admissibility += task_stats[task_pos].rejected_complete_admissibility;
                stats.rejected_complete_dominance += task_stats[task_pos].rejected_complete_dominance;
                stats.removed_complete_dominated  += task_stats[task_pos].removed_complete_dominated;
                stats.rejected_complete_tolerance += task_stats[task_pos].rejected_complete_tolerance;
                batch_final_found += connection_count;
                batch_retained_before_tolerance += before_tolerance;
                slot_results.push_back(
                    SearchSlotResult{
                          .slot = slot
                        , .connection_count = connection_count
                        , .connections = std::move(connections)
                        , .day_path_alternatives = std::move(day_path_alternatives)
                    }
                );
                MATHFP_TRY(validate_od_day_post_layer_result(slot_results.back()));
                MATHFP_TRY(validate_reachability_rejection_stats(task_stats[task_pos]));
                MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(task_stats[task_pos]));

                if (diagnostics.log_projection_details) {
                    log(
                        fmt::format(
                              "search batch projection done: batch={}/{} slot={} kind={} task={} origin={} destination={} interval={} found={:>8}"
                              " retained_before_tolerance={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                              " reachability_pruned={} reachability_detail(phase/budget/unreachable)={}/{}/{}"
                              " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                            , batch_index + 1
                            , batch_count
                            , static_cast<std::int64_t>(task_pos)
                            , to_log_token(slot.kind)
                            , slot.task_ref.has_value()
                                ? std::to_string(slot.task_ref->get())
                                : std::string{"<none>"}
                            , slot.origin.get()
                            , slot.destination.get()
                            , slot.interval.has_value()
                                ? std::to_string(slot.interval->get())
                                : std::string{"<none>"}
                            , slot_results.back().connection_count
                            , before_tolerance
                            , task_stats[task_pos].rejected_complete_admissibility
                            , task_stats[task_pos].rejected_complete_dominance
                            , task_stats[task_pos].rejected_complete_tolerance
                            , task_stats[task_pos].removed_complete_dominated
                            , task_stats[task_pos].rejected_reachability
                            , task_stats[task_pos].reachability_rejections.phase
                            , task_stats[task_pos].reachability_rejections.transfer_budget
                            , task_stats[task_pos].reachability_rejections.unreachable_destination
                            , task_stats[task_pos].rejected_suffix_lower_bound
                            , task_stats[task_pos].suffix_lower_bound_rejections.exact_dominance
                            , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_impedance
                            , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_journey_time
                            , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_transfers
                        )
                        , LogLevel::Info
                    );
                }
            }
            MATHFP_TRY(validate_reachability_rejection_stats(stats));
            MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(stats));

            log(
                fmt::format(
                      "search batch done: {}/{} origin={} interval={} tasks={} found={:>8} completed={:>8}"
                      " retained_before_tolerance={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                      " expanded={:>8} generated={:>8} accepted={:>8}"
                      " rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                      " reachability_detail(phase/budget/unreachable)={}/{}/{} max_frontier={}/{}"
                      " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                      " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={} compact_current={} compact_next={})"
                      " post_layer(candidates/inserted/replaced/max_supports)={}/{}/{}/{}"
                      " paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{}"
                      " late_guard(time_domain/feasibility/reboarding)={}/{}/{}"
                      " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                      " accepted_phase({})"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                    , batch.projection_slots.size()
                    , batch_final_found
                    , stats.completed_connections
                    , batch_retained_before_tolerance
                    , stats.rejected_complete_admissibility
                    , stats.rejected_complete_dominance
                    , stats.rejected_complete_tolerance
                    , stats.removed_complete_dominated
                    , stats.expanded_branches
                    , stats.generated_successors
                    , stats.accepted_branches
                    , stats.rejected_time_domain
                    , stats.rejected_feasibility
                    , stats.rejected_reboarding
                    , stats.rejected_cycles
                    , stats.rejected_transfer_limit
                    , stats.rejected_reachability
                    , stats.rejected_dominance_or_tolerance
                    , stats.reachability_rejections.phase
                    , stats.reachability_rejections.transfer_budget
                    , stats.reachability_rejections.unreachable_destination
                    , stats.max_current_frontier
                    , stats.max_next_frontier
                    , stats.rejected_suffix_lower_bound
                    , stats.suffix_lower_bound_rejections.exact_dominance
                    , stats.suffix_lower_bound_rejections.tolerance_impedance
                    , stats.suffix_lower_bound_rejections.tolerance_journey_time
                    , stats.suffix_lower_bound_rejections.tolerance_transfers
                    , stats.stale_frontier_skipped
                    , stats.c_y_removed_dominated
                    , stats.c_y_removed_stale
                    , stats.frontier_compaction_runs
                    , stats.frontier_compaction_removed
                    , stats.frontier_compaction_removed_current
                    , stats.frontier_compaction_removed_next
                    , stats.post_layer_day_path_candidates
                    , stats.post_layer_day_path_inserted
                    , stats.post_layer_day_path_representative_replaced
                    , stats.post_layer_day_path_supports
                    , stats.paper_timed_lookup_skipped_phase
                    , stats.paper_timed_lookup_skipped_transfer_budget
                    , stats.paper_timed_successor_rejected_time_domain
                    , stats.paper_timed_successor_rejected_same_trip
                    , stats.paper_timed_successor_rejected_same_line
                    , stats.paper_timed_successor_rejected_feasibility
                    , stats.rejected_time_domain
                    , stats.rejected_feasibility
                    , stats.rejected_reboarding
                    , format_walk_lookup_stats(stats.walk_lookup)
                    , format_walk_kind_stats(stats.generated_walk)
                    , format_walk_kind_stats(stats.accepted_walk)
                    , stats.rejected_consecutive_walk
                    , format_branch_phase_stats(stats.accepted_branches_by_phase)
                )
                , LogLevel::Info
            );
            log(
                format_search_pruning_runtime_stats(summarize(stats.pruning))
                , LogLevel::Info
            );
            emit_storage_diagnostics();
            if (od_day_slots) {
                MATHFP_TRY(validate_od_day_production_batch_invariants(
                      batch
                    , stats
                    , make_storage_diagnostics()
                ));
            }

            return slot_results;
        }

    }  // namespace
    namespace {

        [[nodiscard]] std::size_t active_demand_origin_count(
            std::span<const SearchTask> tasks
        ) {
            std::map<ZoneId, mathfp::Unit> origins;
            for (const auto& task : tasks) {
                origins.emplace(task.origin, mathfp::kUnit);
            }
            return origins.size();
        }

        [[nodiscard]] std::size_t search_origin_count(
            std::span<const SearchBatch> batches
        ) {
            std::map<ZoneId, mathfp::Unit> origins;
            for (const auto& batch : batches) {
                origins.emplace(batch.key.origin, mathfp::kUnit);
            }
            return origins.size();
        }

        [[nodiscard]] std::size_t expected_search_tree_count(
              SearchOriginScope          origin_scope
            , std::size_t                declared_zone_count
            , std::span<const SearchTask> tasks
        ) {
            switch (origin_scope) {
                case SearchOriginScope::ActiveDemandOrigins:
                    return active_demand_origin_count(tasks);

                case SearchOriginScope::DeclaredZones:
                    return declared_zone_count;
            }
            return declared_zone_count;
        }

        [[nodiscard]] std::int64_t signed_count_delta(
              std::size_t actual
            , std::size_t expected
        ) noexcept {
            return static_cast<std::int64_t>(actual)
                 - static_cast<std::int64_t>(expected);
        }

        mathfp::Expected<mathfp::Unit> validate_search_execution_projection_contract(
            const SearchExecutionConfig& config
        ) {
            if (config.max_parallel_batches == 0u) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search execution maxParallelBatches must be positive")
                );
            }
            if (config.max_od_day_label_representatives_per_state == 0u) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search execution maxOdDayLabelRepresentativesPerState must be positive")
                );
            }
            if (config.estimated_memory_mb_per_parallel_batch == 0u) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search execution estimatedMemoryMbPerParallelBatch must be positive")
                );
            }
            if (config.mode == SearchExecutionMode::IntervalLocal
                && config.result_projection != SearchResultProjection::DemandTasks) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("interval-local search supports only demand-task projection")
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                );
            }
            const auto origin_period_projection =
                   config.result_projection == SearchResultProjection::CompletionTargets
                || config.result_projection == SearchResultProjection::OdDayPairs;
            if (origin_period_projection && config.mode != SearchExecutionMode::OriginPeriod) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("tree-level result projection requires origin-period search")
                        .ctx("execution_mode", std::string(to_string(config.mode)))
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                );
            }
            if (config.partial_retention_scope == SearchPartialRetentionScope::TreeGlobal
                && !origin_period_projection) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("tree-global partial retention requires a tree-level result projection")
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                );
            }
            switch (config.formulation) {
                case AssignmentCalculationFormulation::OdDayAssignment:
                    if (!is_required_od_day_assignment_profile(config)) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("OD-day formulation must use the production OD-day search profile")
                                .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                                .ctx("mode", std::string(to_string(config.mode)))
                                .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                                .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                                .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                                .ctx("result_projection", std::string(to_string(config.result_projection)))
                                .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                                .ctx("max_parallel_batches", static_cast<std::int64_t>(config.max_parallel_batches))
                                .ctx(
                                      "max_od_day_label_representatives_per_state"
                                    , static_cast<std::int64_t>(
                                          config.max_od_day_label_representatives_per_state
                                      )
                                  )
                        );
                    }
                    break;

                case AssignmentCalculationFormulation::TimedConnectionDiagnostics:
                    if (!is_timed_connection_diagnostics_profile(config)) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("timed connection search contour is diagnostic-only")
                                .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                                .ctx("mode", std::string(to_string(config.mode)))
                                .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                                .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                                .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                                .ctx("result_projection", std::string(to_string(config.result_projection)))
                                .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                        );
                    }
                    break;

                case AssignmentCalculationFormulation::AllZoneSearch:
                    if (!is_all_zone_search_diagnostics_profile(config)) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("all-zone search contour is diagnostic-only")
                                .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                                .ctx("result_projection", std::string(to_string(config.result_projection)))
                                .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                        );
                    }
                    break;

                case AssignmentCalculationFormulation::DemandTaskAssignment:
                    return mathfp::unexpected(
                        mathfp::invalid_arg("legacy demand-task timed assignment is disabled; use od_day_assignment for production or timed_connection_diagnostics for search-only diagnostics")
                            .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                            .ctx("mode", std::string(to_string(config.mode)))
                            .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                            .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                            .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                            .ctx("result_projection", std::string(to_string(config.result_projection)))
                            .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                    );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<SearchTimeDomainSummary> summarize_batch_search_domains(
            std::span<const SearchBatch> batches
        ) {
            std::vector<SearchTimeWindow> windows;
            for (const auto& batch : batches) {
                if (batch.departure_domain == nullptr) {
                    continue;
                }
                windows.insert(
                      windows.end()
                    , batch.departure_domain->windows.begin()
                    , batch.departure_domain->windows.end()
                );
            }
            if (windows.empty()) {
                return SearchTimeDomainSummary{};
            }
            MATHFP_TRY_LET(
                  SearchTimeDomain
                , domain
                , make_search_time_domain(std::move(windows))
            );
            return summarize(domain);
        }

        mathfp::Expected<mathfp::Unit> validate_search_batch_projection_contract(
              std::span<const SearchBatch> batches
            , SearchExecutionMode          execution_mode
            , SearchResultProjection       result_projection
            , std::span<const SearchTask>  tasks
        ) {
            for (std::size_t batch_pos = 0; batch_pos < batches.size(); ++batch_pos) {
                const auto& batch = batches[batch_pos];
                if (batch.departure_domain == nullptr) {
                    return mathfp::unexpected(
                        mathfp::internal_error("search batch has no departure domain")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                    );
                }

                switch (execution_mode) {
                    case SearchExecutionMode::IntervalLocal:
                        if (!batch.key.interval.has_value()) {
                            return mathfp::unexpected(
                                mathfp::internal_error("interval-local search batch has no interval key")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("origin", batch.key.origin.get())
                            );
                        }
                        break;

                    case SearchExecutionMode::OriginPeriod:
                        if (batch.key.interval.has_value()) {
                            return mathfp::unexpected(
                                mathfp::internal_error("origin-period search batch unexpectedly has interval key")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("origin", batch.key.origin.get())
                                    .ctx("interval", batch.key.interval->get())
                            );
                        }
                        break;
                }

                std::map<ZoneId, mathfp::Unit> target_destinations;
                for (std::size_t target_pos = 0; target_pos < batch.completion_targets.size(); ++target_pos) {
                    const auto& target = batch.completion_targets[target_pos];
                    if (target.index.get() != static_cast<std::int64_t>(target_pos)) {
                        return mathfp::unexpected(
                            mathfp::internal_error("search completion target index does not match its position")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("target_position", static_cast<std::int64_t>(target_pos))
                                .ctx("target_index", target.index.get())
                        );
                    }
                    const auto [_, inserted] = target_destinations.emplace(
                          target.destination
                        , mathfp::kUnit
                    );
                    if (!inserted) {
                        return mathfp::unexpected(
                            mathfp::internal_error("search batch contains duplicate completion target destination")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("origin", batch.key.origin.get())
                                .ctx("destination", target.destination.get())
                        );
                    }
                }

                for (std::size_t task_pos = 0; task_pos < batch.projection_slots.size(); ++task_pos) {
                    const auto& slot = batch.projection_slots[task_pos];
                    if (slot.origin != batch.key.origin) {
                        return mathfp::unexpected(
                            mathfp::internal_error("search projection slot origin does not match tree origin")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("tree_origin", batch.key.origin.get())
                                .ctx("slot_origin", slot.origin.get())
                        );
                    }
                    if (!target_destinations.contains(slot.destination)) {
                        return mathfp::unexpected(
                            mathfp::internal_error("search projection slot has no matching completion target")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("origin", slot.origin.get())
                                .ctx("destination", slot.destination.get())
                        );
                    }

                    switch (result_projection) {
                        case SearchResultProjection::DemandTasks: {
                            if (slot.kind != SearchProjectionSlotKind::DemandTask
                                || slot.task == nullptr
                                || !slot.result_index.has_value()
                                || !slot.task_ref.has_value()) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("demand projection slot has invalid task payload")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                );
                            }
                            if (*slot.result_index >= tasks.size()) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("demand projection slot result index is out of range")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                        .ctx("result_index", static_cast<std::int64_t>(*slot.result_index))
                                        .ctx("task_count", static_cast<std::int64_t>(tasks.size()))
                                );
                            }
                            const auto& task = *slot.task;
                            if (*slot.task_ref != task.index) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("search projection slot task ref disagrees with task payload")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("task_position", static_cast<std::int64_t>(task_pos))
                                        .ctx("slot_task", slot.task_ref->get())
                                        .ctx("task", task.index.get())
                                );
                            }
                            if (slot.origin != task.origin || slot.destination != task.destination) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("search projection slot OD disagrees with task payload")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("task", task.index.get())
                                );
                            }
                            if (execution_mode == SearchExecutionMode::IntervalLocal
                                && task.interval.id != *batch.key.interval) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("interval-local projection task interval does not match batch interval")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("task", task.index.get())
                                        .ctx("batch_interval", batch.key.interval->get())
                                        .ctx("task_interval", task.interval.id.get())
                                );
                            }
                            break;
                        }

                        case SearchResultProjection::OdDayPairs:
                            if (slot.kind != SearchProjectionSlotKind::OdDayPair
                                || !slot.completion_target.has_value()
                                || slot.task != nullptr
                                || slot.task_ref.has_value()
                                || slot.result_index.has_value()
                                || slot.interval.has_value()) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("OD-day projection slot has invalid payload")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                );
                            }
                            if (slot.completion_target->get()
                                != static_cast<std::int64_t>(task_pos)) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("OD-day projection slot index does not match its position")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                        .ctx("target_index", slot.completion_target->get())
                                );
                            }
                            if (batch.completion_targets[task_pos].destination
                                != slot.destination) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("OD-day projection slot destination disagrees with target position")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                        .ctx("slot_destination", slot.destination.get())
                                        .ctx("target_destination", batch.completion_targets[task_pos].destination.get())
                                );
                            }
                            break;

                        case SearchResultProjection::CompletionTargets:
                            if (slot.kind != SearchProjectionSlotKind::CompletionTarget
                                || !slot.completion_target.has_value()
                                || slot.task != nullptr
                                || slot.task_ref.has_value()
                                || slot.result_index.has_value()) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("completion-target projection slot has invalid payload")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                );
                            }
                            if (slot.completion_target->get()
                                != static_cast<std::int64_t>(task_pos)) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("completion-target projection slot index does not match its position")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                        .ctx("target_index", slot.completion_target->get())
                                );
                            }
                            if (batch.completion_targets[task_pos].destination
                                != slot.destination) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("completion-target projection slot destination disagrees with target position")
                                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                        .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                        .ctx("slot_destination", slot.destination.get())
                                        .ctx("target_destination", batch.completion_targets[task_pos].destination.get())
                                );
                            }
                            break;
                    }
                }
            }

            return mathfp::kUnit;
        }

        [[nodiscard]] std::size_t search_slot_connection_count(
            std::span<const SearchSlotResult> results
        ) noexcept {
            std::size_t total = 0;
            for (const auto& result : results) {
                total += result.connection_count;
            }
            return total;
        }

        [[nodiscard]] std::size_t search_batch_worker_count(
              std::size_t batch_count
            , const SearchExecutionConfig& config
        ) noexcept {
            if (batch_count == 0u || config.max_parallel_batches == 0u) {
                return 0u;
            }
            const auto hardware = std::max(
                  1u
                , std::thread::hardware_concurrency()
            );
            auto worker_count = std::min(
                  batch_count
                , std::min<std::size_t>(
                      static_cast<std::size_t>(hardware)
                    , config.max_parallel_batches
                  )
            );
            if (config.max_parallel_memory_mb.has_value()
                && config.estimated_memory_mb_per_parallel_batch > 0u) {
                const auto memory_limited_workers = std::max<std::size_t>(
                      1u
                    , *config.max_parallel_memory_mb
                        / config.estimated_memory_mb_per_parallel_batch
                );
                worker_count = std::min(worker_count, memory_limited_workers);
            }
            return worker_count;
        }

        [[nodiscard]] ConnectionSearchResult materialize_demand_task_search_result(
              std::span<const SearchTask>       tasks
            , std::vector<SearchSlotResult>     slot_results
        ) {
            ConnectionSearchResult result;
            result.task_results.reserve(tasks.size());
            for (const auto& task : tasks) {
                result.task_results.push_back(
                    SearchTaskResult{
                          .task        = task
                        , .connections = {}
                    }
                );
            }

            for (auto& slot_result : slot_results) {
                if (slot_result.slot.kind != SearchProjectionSlotKind::DemandTask
                    || !slot_result.slot.result_index.has_value()) {
                    continue;
                }
                result.task_results[*slot_result.slot.result_index].connections =
                    std::move(slot_result.connections);
            }
            return result;
        }

        [[nodiscard]] OriginDaySearchResult materialize_origin_day_search_result(
              ZoneId                        origin
            , std::vector<SearchSlotResult> slot_results
        ) {
            OriginDaySearchResult result{
                  .origin       = origin
                , .pair_results = {}
            };
            result.pair_results.reserve(slot_results.size());
            for (auto& slot_result : slot_results) {
                if (slot_result.slot.kind != SearchProjectionSlotKind::OdDayPair) {
                    continue;
                }
                result.pair_results.push_back(
                    OdDayPairResult{
                          .origin      = slot_result.slot.origin
                        , .destination = slot_result.slot.destination
                        , .alternatives = std::move(slot_result.day_path_alternatives)
                    }
                );
            }
            return result;
        }

        struct CountOnlyAllZoneSearchResultSink final {
            std::mutex mutex{};
            std::map<ZoneId, std::map<ZoneId, std::size_t>> counts{};

            mathfp::Expected<mathfp::Unit> accept(
                std::vector<SearchSlotResult> slot_results
            ) {
                std::lock_guard lock{ mutex };
                for (auto& slot_result : slot_results) {
                    if (slot_result.slot.kind != SearchProjectionSlotKind::CompletionTarget) {
                        continue;
                    }
                    counts[slot_result.slot.origin][slot_result.slot.destination] =
                        slot_result.connection_count;
                }
                return mathfp::kUnit;
            }

            [[nodiscard]] AllZoneConnectionSearchResult materialize() {
                std::lock_guard lock{ mutex };
                AllZoneConnectionSearchResult result;
                result.tree_results.reserve(counts.size());
                for (const auto& [origin, targets] : counts) {
                    AllZoneTreeResult tree{
                          .origin = origin
                        , .target_results = {}
                    };
                    tree.target_results.reserve(targets.size());
                    for (const auto& [destination, connection_count] : targets) {
                        tree.target_results.push_back(
                            AllZoneTargetResult{
                                  .origin           = origin
                                , .destination      = destination
                                , .connection_count = connection_count
                                , .connections      = {}
                            }
                        );
                    }
                    result.tree_results.push_back(std::move(tree));
                }
                return result;
            }
        };

    }  // namespace
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        const auto execution_mode = execution.config.mode;
        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::DemandTasks) {
            return mathfp::unexpected(
                mathfp::invalid_arg("ConnectionSearchResult search currently supports only DemandTasks result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));

        both("search: branch-and-bound");
        log(
            fmt::format(
                "search input: route_segments = {:>8}  connection_segments = {:>8}"
                "  tasks = {:>8}  execution_mode = {}  max_transfers = {}"
                , network.route_segments     .size()
                , network.connection_segments.size()
                , tasks.size()
                , to_string(execution_mode)
                , params.transfers.max_transfers.get()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search cost: mode={}  fare_scale={:.6f}  capacity_iteration={}  capacity_index(loads/capacities/trips/prefixes)={}/{}/{}/{}"
                , to_string(search_cost.mode)
                , search_cost.fare_scale
                , diagnostics.capacity_iteration
                , search_cost.capacity.index.load_positions.size()
                , search_cost.capacity.index.capacity_positions.size()
                , search_cost.capacity.index.capacity_trip_positions.size()
                , search_cost.capacity.index.penalty_prefixes.size()
            )
            , LogLevel::Info
        );

        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , default_pruning_execution
            , plan_search_pruning_execution(
                  SearchPruningModelConfig{
                      .requested_state_space =
                          SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  }
                , SearchPruningRolloutStage::Disabled
                , params.search_tolerances
            )
        );
        const auto& effective_pruning_execution =
            pruning_execution != nullptr
                ? *pruning_execution
                : default_pruning_execution;

        log(
            fmt::format(
                "search setup: tasks = {:>8}  fare_scale = {:.6f}  execution_mode = {}"
                , tasks.size()
                , search_cost.fare_scale
                , to_string(execution_mode)
            )
            , LogLevel::Info
        );
        log(
            format_search_pruning_execution_summary(summarize(effective_pruning_execution))
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search projection contract: result_projection={} partial_retention_scope={} complete_retention=projection_slot_local"
                , to_string(execution.config.result_projection)
                , to_string(execution.config.partial_retention_scope)
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "timed/task contour: status=diagnostic_only formulation={} diagnostic_mode={} carrier=raw_timed_branch_trace result_projection={} demand_intervals_in_search={} od_day_production_separate=yes"
                , to_string(execution.config.formulation)
                , execution.config.diagnostic_mode ? "true" : "false"
                , to_string(execution.config.result_projection)
                , execution_mode == SearchExecutionMode::IntervalLocal
                    ? "interval_local"
                    : "origin_period_domain"
            )
            , LogLevel::Info
        );

        if (tasks.empty()) {
            status("search: no positive-demand search tasks available");
        }

        std::vector<SearchTreeJob> origin_period_tree_jobs;
        std::vector<SearchBatch>   batches;
        if (execution_mode == SearchExecutionMode::OriginPeriod) {
            if (search_cost.mode != SearchCostMode::BaseOnly) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search currently supports only base search cost")
                        .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
                );
            }
            if (execution.time_domain_execution == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search requires SearchTimeDomainExecution")
                );
            }
            mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
                execution.config.origin_scope == SearchOriginScope::DeclaredZones
                    ? build_declared_origin_period_search_tree_jobs(
                          execution.declared_zones
                        , tasks
                        , *execution.time_domain_execution
                        , execution.config.destination_scope
                      )
                    : build_origin_period_search_tree_jobs(
                          tasks
                        , *execution.time_domain_execution
                        , execution.config.destination_scope
                        , execution.declared_zones
                      );
            if (!tree_jobs_result) {
                return mathfp::unexpected(std::move(tree_jobs_result.error()));
            }
            auto tree_jobs = std::move(*tree_jobs_result);
            origin_period_tree_jobs = std::move(tree_jobs);
            MATHFP_TRY_LET(
                  std::vector<SearchBatch>
                , origin_batches
                , build_origin_period_search_batches(
                      tasks
                    , origin_period_tree_jobs
                    , execution.config.result_projection
                )
            );
            batches = std::move(origin_batches);
            log(
                fmt::format(
                    "search tree jobs: mode={} jobs = {:>8}  tasks = {:>8}"
                    , to_string(execution_mode)
                    , origin_period_tree_jobs.size()
                    , tasks.size()
                )
                , LogLevel::Info
            );
        } else {
            batches = build_interval_local_search_batches(tasks);
        }
        MATHFP_TRY(validate_search_batch_projection_contract(
              batches
            , execution_mode
            , execution.config.result_projection
            , tasks
        ));

        const auto residual_reverse_graph = build_residual_reverse_graph(
              network.route_segments
            , network.connection_segments
        );
        const auto batch_execution_diagnostics = summarize_search_batches(batches);
        const auto expected_tree_count = expected_search_tree_count(
              execution.config.origin_scope
            , diagnostics.declared_zone_count
            , tasks
        );
        MATHFP_TRY_LET(
              SearchTimeDomainSummary
            , search_domain_summary
            , summarize_batch_search_domains(batches)
        );
        log(
            fmt::format(
                "search batching: mode={} batches = {:>8}  tasks = {:>8}"
                , to_string(execution_mode)
                , batches.size()
                , tasks.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search diagnostics: search_execution_mode={} origin_scope={} time_domain_source={} destination_scope={} result_projection={} partial_retention_scope={} phase_invariant_validation={}"
                  " tree_count={} expected_tree_count={} tree_count_delta={} search_origin_count={} declared_zone_count={} declared_zone_request_count={}"
                  " active_demand_origin_count={} completion_target_count={} projection_slot_count={}"
                  " zero_completion_target_tree_count={} zero_projection_task_tree_count={}"
                  " max_completion_targets_per_tree={} max_projection_tasks_per_tree={}"
                  " search_domain_summary=\"{}\""
                , to_string(execution_mode)
                , to_string(execution.config.origin_scope)
                , to_string(execution.config.time_domain_source)
                , to_string(execution.config.destination_scope)
                , to_string(execution.config.result_projection)
                , to_string(execution.config.partial_retention_scope)
                , diagnostics.validate_phase_invariants ? "on" : "off"
                , batches.size()
                , expected_tree_count
                , signed_count_delta(batches.size(), expected_tree_count)
                , search_origin_count(batches)
                , diagnostics.declared_zone_count
                , execution.declared_zones.size()
                , active_demand_origin_count(tasks)
                , batch_execution_diagnostics.completion_target_count
                , batch_execution_diagnostics.projection_task_count
                , batch_execution_diagnostics.zero_completion_target_tree_count
                , batch_execution_diagnostics.zero_projection_task_tree_count
                , batch_execution_diagnostics.max_completion_targets_per_tree
                , batch_execution_diagnostics.max_projection_tasks_per_tree
                , format_search_time_domain_summary(search_domain_summary)
            )
            , LogLevel::Info
        );

        std::vector<SearchSlotResult> slot_results;
        for (std::size_t i = 0; i < batches.size(); ++i) {
            const auto& batch = batches[i];
            const auto total_found = search_slot_connection_count(slot_results);
            if (i == 0 || (i % kTaskProgressStep) == 0 || (i + 1) == batches.size()) {
                status(
                    fmt::format(
                          "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} total_found={}"
                        , i + 1
                        , batches.size()
                        , batch.key.origin.get()
                        , format_batch_interval(batch.key.interval)
                        , batch.projection_slots.size()
                        , batch.completion_targets.size()
                        , diagnostics.capacity_iteration
                        , total_found
                    )
                );
            }
            log(
                fmt::format(
                      "search batch start: {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} cumulative_found={}"
                    , i + 1
                    , batches.size()
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                    , batch.projection_slots.size()
                    , batch.completion_targets.size()
                    , diagnostics.capacity_iteration
                    , total_found
                )
                , LogLevel::Info
            );
            MATHFP_TRY_LET(
                  std::vector<SearchSlotResult>
                , batch_slot_results
                , search_batch_connections(
                      batch
                    , network
                    , residual_reverse_graph
                    , params
                    , search_cost
                    , choice_config
                    , assignment_period
                    , admissibility_config
                    , effective_pruning_execution
                    , complete_connection_dominance
                    , execution.config.partial_retention_scope
                    , diagnostics
                    , i
                    , batches.size()
                )
            );
            slot_results.insert(
                  slot_results.end()
                , std::make_move_iterator(batch_slot_results.begin())
                , std::make_move_iterator(batch_slot_results.end())
            );
        }

        auto result = materialize_demand_task_search_result(
              tasks
            , std::move(slot_results)
        );
        log(
            fmt::format(
                  "search result: tasks = {:>8}  connections = {:>8}"
                , result.task_results.size()
                , search_connection_count(result)
            )
            , LogLevel::Info
        );
        both("search: branch-and-bound done");
        return result;
    }

    mathfp::Expected<AllZoneConnectionSearchResult> search_all_zone_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::CompletionTargets) {
            return mathfp::unexpected(
                mathfp::invalid_arg("AllZoneConnectionSearchResult search requires CompletionTargets result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        if (execution.config.mode != SearchExecutionMode::OriginPeriod) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone search requires origin-period execution")
                    .ctx("execution_mode", std::string(to_string(execution.config.mode)))
            );
        }
        if (search_cost.mode != SearchCostMode::BaseOnly) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone completion-target search currently supports only base search cost")
                    .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
            );
        }
        if (execution.time_domain_execution == nullptr) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone origin-period search requires SearchTimeDomainExecution")
            );
        }

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));

        both("search: all-zone branch-and-bound");
        log(
            fmt::format(
                  "all-zone projection contract: partial_retention_scope={} complete_retention=completion_target_slot"
                , to_string(execution.config.partial_retention_scope)
            )
            , LogLevel::Info
        );
        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , default_pruning_execution
            , plan_search_pruning_execution(
                  SearchPruningModelConfig{
                      .requested_state_space =
                          SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  }
                , SearchPruningRolloutStage::Disabled
                , params.search_tolerances
            )
        );
        const auto& effective_pruning_execution =
            pruning_execution != nullptr
                ? *pruning_execution
                : default_pruning_execution;

        mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
            execution.config.origin_scope == SearchOriginScope::DeclaredZones
                ? build_declared_origin_period_search_tree_jobs(
                      execution.declared_zones
                    , tasks
                    , *execution.time_domain_execution
                    , execution.config.destination_scope
                  )
                : build_origin_period_search_tree_jobs(
                      tasks
                    , *execution.time_domain_execution
                    , execution.config.destination_scope
                    , execution.declared_zones
                  );
        if (!tree_jobs_result) {
            return mathfp::unexpected(std::move(tree_jobs_result.error()));
        }
        auto tree_jobs = std::move(*tree_jobs_result);
        MATHFP_TRY_LET(
              std::vector<SearchBatch>
            , batches
            , build_origin_period_search_batches(
                  tasks
                , tree_jobs
                , execution.config.result_projection
            )
        );
        MATHFP_TRY(validate_search_batch_projection_contract(
              batches
            , execution.config.mode
            , execution.config.result_projection
            , tasks
        ));

        const auto residual_reverse_graph = build_residual_reverse_graph(
              network.route_segments
            , network.connection_segments
        );
        const auto batch_execution_diagnostics = summarize_search_batches(batches);
        const auto expected_tree_count = expected_search_tree_count(
              execution.config.origin_scope
            , diagnostics.declared_zone_count
            , tasks
        );
        log(
            fmt::format(
                  "all-zone search comparison diagnostics: trees={} expected_trees={} tree_count_delta={} batches={} targets={} projection_slots={} partial_retention_scope={} phase_invariant_validation={} result_sink=count_only"
                , tree_jobs.size()
                , expected_tree_count
                , signed_count_delta(tree_jobs.size(), expected_tree_count)
                , batches.size()
                , batch_execution_diagnostics.completion_target_count
                , batch_execution_diagnostics.projection_task_count
                , to_string(execution.config.partial_retention_scope)
                , diagnostics.validate_phase_invariants ? "on" : "off"
            )
            , LogLevel::Info
        );

        const auto worker_count = search_batch_worker_count(
              batches.size()
            , execution.config
        );
        log(
            fmt::format(
                  "all-zone search parallel execution: workers={} batches={} max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={} fast_fail=enabled"
                , worker_count
                , batches.size()
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );

        std::atomic<std::size_t> next_batch{ 0u };
        std::atomic<std::size_t> completed_batches{ 0u };
        std::atomic<std::size_t> cancelled_batches{ 0u };
        std::atomic<std::size_t> found_connections{ 0u };
        SearchCancellationToken cancellation;
        CountOnlyAllZoneSearchResultSink result_sink;
        std::vector<std::future<mathfp::Expected<mathfp::Unit>>> workers;
        workers.reserve(worker_count);

        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.push_back(
                std::async(
                      std::launch::async
                    , [&, worker]() -> mathfp::Expected<mathfp::Unit> {
                          for (;;) {
                              if (search_cancelled(&cancellation)) {
                                  return mathfp::kUnit;
                              }
                              const auto i = next_batch.fetch_add(
                                    1u
                                  , std::memory_order_relaxed
                              );
                              if (i >= batches.size()) {
                                  return mathfp::kUnit;
                              }

                              const auto& batch = batches[i];
                              if (
                                     i == 0
                                  || (i % kTaskProgressStep) == 0
                                  || (i + 1) == batches.size()
                              ) {
                                  status(
                                      fmt::format(
                                            "all-zone search: worker={} batch {}/{} origin={} targets={} projection_slots={} completed={} total_found={}"
                                          , worker
                                          , i + 1
                                          , batches.size()
                                          , batch.key.origin.get()
                                          , batch.completion_targets.size()
                                          , batch.projection_slots.size()
                                          , completed_batches.load(std::memory_order_relaxed)
                                          , found_connections.load(std::memory_order_relaxed)
                                      )
                                  );
                              }

                              auto results_result = search_batch_connections(
                                        batch
                                      , network
                                      , residual_reverse_graph
                                      , params
                                      , search_cost
                                      , choice_config
                                      , assignment_period
                                      , admissibility_config
                                      , effective_pruning_execution
                                      , complete_connection_dominance
                                      , execution.config.partial_retention_scope
                                      , diagnostics
                                      , i
                                      , batches.size()
                                      , nullptr
                                      , OdDayLabelRetentionConfig{}
                                      , &cancellation
                                  );
                              if (!results_result) {
                                  request_search_cancellation(&cancellation);
                                  return mathfp::unexpected(std::move(results_result.error()));
                              }
                              if (search_cancelled(&cancellation)) {
                                  cancelled_batches.fetch_add(1u, std::memory_order_relaxed);
                                  return mathfp::kUnit;
                              }
                              auto results = std::move(*results_result);
                              found_connections.fetch_add(
                                    search_slot_connection_count(results)
                                  , std::memory_order_relaxed
                              );
                              auto sink_result = result_sink.accept(std::move(results));
                              if (!sink_result) {
                                  request_search_cancellation(&cancellation);
                                  return mathfp::unexpected(std::move(sink_result.error()));
                              }
                              completed_batches.fetch_add(1u, std::memory_order_relaxed);
                          }
                      }
                )
            );
        }

        mathfp::Expected<mathfp::Unit> first_worker_error = mathfp::kUnit;
        for (auto& worker : workers) {
            auto worker_result = worker.get();
            if (!worker_result && first_worker_error) {
                request_search_cancellation(&cancellation);
                first_worker_error = mathfp::unexpected(
                    std::move(worker_result.error())
                );
            }
        }
        if (cancelled_batches.load(std::memory_order_relaxed) != 0u) {
            log(
                fmt::format(
                      "all-zone search fast-fail cancellation: cancelled_batches={}"
                    , cancelled_batches.load(std::memory_order_relaxed)
                )
                , LogLevel::Warning
            );
        }
        MATHFP_TRY(std::move(first_worker_error));

        auto result = result_sink.materialize();
        log(
            fmt::format(
                  "all-zone search result: trees = {:>8}  expected_trees = {:>8}  connections = {:>8}"
                , result.tree_results.size()
                , expected_tree_count
                , search_connection_count(result)
            )
            , LogLevel::Info
        );
        both("search: all-zone branch-and-bound done");
        return result;
    }

    mathfp::Expected<mathfp::Unit> search_od_day_paths_by_origin_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , OdDayOriginResultSink      origin_sink
        , SearchDiagnosticsContext diagnostics
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::OdDayPairs) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OdDayPathSearchResult search requires OdDayPairs result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        if (execution.config.mode != SearchExecutionMode::OriginPeriod) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search requires origin-period execution")
                    .ctx("execution_mode", std::string(to_string(execution.config.mode)))
            );
        }
        if (search_cost.mode != SearchCostMode::BaseOnly) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search currently supports only base search cost")
                    .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
            );
        }
        if (execution.time_domain_execution == nullptr) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day origin-period search requires SearchTimeDomainExecution")
            );
        }
        if (!origin_sink) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day by-origin search requires a result sink")
            );
        }
        if (execution.config.partial_retention_scope != SearchPartialRetentionScope::TreeGlobal) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search requires tree_global partial retention")
                    .ctx("partial_retention_scope", std::string(to_string(execution.config.partial_retention_scope)))
            );
        }

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));
        const auto od_day_contract = make_od_day_path_search_contract(
            diagnostics.declared_zone_count
        );
        if (!satisfies_od_day_path_search_contract(od_day_contract)) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day search production contract is not satisfied")
                    .ctx(
                          "declared_origin_count"
                        , static_cast<std::int64_t>(od_day_contract.declared_origin_count)
                    )
            );
        }

        both("search: OD-day branch-and-bound");
        const auto day_path_retention_config = DayPathRetentionConfig{};
        const auto od_day_label_retention_config =
            od_day_label_retention_config_of(execution.config);
        const auto day_path_alternative_limit =
            day_path_retention_config.max_alternatives_per_od.has_value()
                ? std::to_string(*day_path_retention_config.max_alternatives_per_od)
                : std::string("unbounded");
        const auto day_path_support_limit =
            day_path_retention_config.max_supports_per_path.has_value()
                ? std::to_string(*day_path_retention_config.max_supports_per_path)
                : std::string("unbounded");
        log(
            fmt::format(
                  "OD-day projection contract: paper=connection_tree partial_retention_scope={} tree_label=network_node_c_y c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink support=connection_segment_witness dominance=dep_arr_imp_nt tolerance=node_local production_carrier=compact_connection_segment_prefix path_identity=compact_prefix supply_graph=preprocessed_connection_segment_index frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_contract=single_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available late_guard=assert_only transfer_walk=first_class_segment composite_transfer_walk=disabled_in_production label_representatives_per_state={} tree_bounds=c_y_before_day_path_sink suffix_bound=disabled_for_od_day completed_connection_projection=immediate_day_path_sink od_alternative_retention=production_slots_only signature=route_stop_line_pattern day_path_retention_policy={} max_alternatives_per_od={} max_supports_per_path={} computation_contract={}"
                , to_string(execution.config.partial_retention_scope)
                , od_day_label_retention_config.max_representatives_per_label
                , day_path_retention_limit_policy_name(
                      day_path_retention_config.limit_policy
                  )
                , day_path_alternative_limit
                , day_path_support_limit
                , to_log_token(OdDaySearchComputationContract::PaperConnectionSegmentTree)
            )
            , LogLevel::Info
        );
        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , default_pruning_execution
            , plan_search_pruning_execution(
                  SearchPruningModelConfig{
                      .requested_state_space =
                          SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  }
                , SearchPruningRolloutStage::Disabled
                , params.search_tolerances
            )
        );
        const auto& effective_pruning_execution =
            pruning_execution != nullptr
                ? *pruning_execution
                : default_pruning_execution;

        mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
            execution.config.origin_scope == SearchOriginScope::DeclaredZones
                ? build_declared_origin_period_search_tree_jobs(
                      execution.declared_zones
                    , tasks
                    , *execution.time_domain_execution
                    , execution.config.destination_scope
                  )
                : build_origin_period_search_tree_jobs(
                      tasks
                    , *execution.time_domain_execution
                    , execution.config.destination_scope
                    , execution.declared_zones
                  );
        if (!tree_jobs_result) {
            return mathfp::unexpected(std::move(tree_jobs_result.error()));
        }
        auto tree_jobs = std::move(*tree_jobs_result);
        MATHFP_TRY_LET(
              std::vector<SearchBatch>
            , batches
            , build_origin_period_search_batches(
                  tasks
                , tree_jobs
                , execution.config.result_projection
            )
        );
        MATHFP_TRY(validate_search_batch_projection_contract(
              batches
            , execution.config.mode
            , execution.config.result_projection
            , tasks
        ));

        /*
         * The paper OD-day production contour does not use suffix reachability
         * masks as a branch filter. Keep an empty graph only to satisfy the
         * shared batch-search signature; timed/diagnostic contours still build
         * and use residual reachability in their own entry points.
         */
        const ResidualReverseGraph residual_reverse_graph{};
        const auto batch_execution_diagnostics = summarize_search_batches(batches);
        const auto expected_tree_count = expected_search_tree_count(
              execution.config.origin_scope
            , diagnostics.declared_zone_count
            , tasks
        );
        if (tree_jobs.size() != expected_tree_count) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day search must build exactly one tree per declared origin")
                    .ctx("tree_count", static_cast<std::int64_t>(tree_jobs.size()))
                    .ctx("expected_tree_count", static_cast<std::int64_t>(expected_tree_count))
                    .ctx("declared_zone_count", static_cast<std::int64_t>(diagnostics.declared_zone_count))
                    .ctx("declared_zone_request_count", static_cast<std::int64_t>(execution.declared_zones.size()))
            );
        }
        if (execution.config.origin_scope == SearchOriginScope::DeclaredZones
            && tree_jobs.size() != execution.declared_zones.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day declared-origin tree count disagrees with declared zone support")
                    .ctx("tree_count", static_cast<std::int64_t>(tree_jobs.size()))
                    .ctx("declared_zone_request_count", static_cast<std::int64_t>(execution.declared_zones.size()))
            );
        }
        MATHFP_TRY(validate_od_day_production_batches(
              batches
            , expected_tree_count
            , execution.config.destination_scope
            , execution.config.destination_scope == SearchDestinationScope::DeclaredZones
                ? execution.declared_zones.size()
                : batch_execution_diagnostics.max_completion_targets_per_tree
        ));
        log(
            fmt::format(
                  "OD-day computational profile: contour=paper_branch_and_bound production_carrier=compact_connection_segment_prefix branch_projection_state=none reachability_prefilter=disabled_not_built reachability_masks=disabled supply_graph=preprocessed_connection_segment_index frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_generation=single_paper_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available transfer_walk_successor=first_class_connection_segment composite_transfer_walk=disabled_in_production walk_successor_lookup=lazy_phase_specific walk_indices=access_transfer_egress tree_label_scope=network_node_c_y c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink dominance=dep_arr_imp_nt tolerance=node_local path_identity=compact_prefix od_signature=route_stop_line_pattern structural_day_contour=diagnostics_only trees={} destinations={} time_horizon=service_day result=post_layer_day_path_support_sets split_contract=paper_connection_split split_interval_admissibility=all_interval_admissible_timed_supports single_best_support=disabled split_load=lazy_support_envelope primary_load=elementary_segment_loads max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={}"
                , tree_jobs.size()
                , execution.config.destination_scope == SearchDestinationScope::DeclaredZones
                    ? execution.declared_zones.size()
                    : batch_execution_diagnostics.max_completion_targets_per_tree
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day paper connection segment tree: connection_segments={} route_segments={} timed_buckets={} boarding_stop_buckets={} access_walks={} transfer_walks={} egress_walks={}"
                , network.connection_segments.size()
                , network.route_segments.size()
                , network.connection_index.timed_buckets.size()
                , network.connection_index.boarding_stop_buckets.size()
                , network.connection_index.access_walk_order.size()
                , network.connection_index.transfer_walk_order.size()
                , network.connection_index.egress_walk_order.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day search diagnostics: paper_clauses=successor_then_c_y_then_sink c_y_scope=node_local destination_in_expansion=no demand_intervals_in_search=no raw_complete_retention=no trees={} expected_trees={} tree_count_delta={} batches={} targets={} projection_slots={} partial_retention_scope={} phase_invariant_validation={} result_sink=origin"
                , tree_jobs.size()
                , expected_tree_count
                , signed_count_delta(tree_jobs.size(), expected_tree_count)
                , batches.size()
                , batch_execution_diagnostics.completion_target_count
                , batch_execution_diagnostics.projection_task_count
                , to_string(execution.config.partial_retention_scope)
                , diagnostics.validate_phase_invariants ? "on" : "off"
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day paper compliance: paper_reference=connection_segment_tree one_tree_per_origin={} trees={} expected_trees={} service_period=day demand_intervals_in_tree=no destination_in_expansion=no successor_order=lookup_then_feasibility_then_c_y_then_enqueue c_y_scope=node_local c_y_key=physical_y c_y_applies_to=all_connection_segments day_path_layer=post_layer production_result=day_path_support_sets raw_completed_connections_retained=no"
                , tree_jobs.size() == expected_tree_count ? "true" : "false"
                , tree_jobs.size()
                , expected_tree_count
            )
            , LogLevel::Info
        );

        const auto worker_count = search_batch_worker_count(
              batches.size()
            , execution.config
        );
        log(
            fmt::format(
                  "OD-day search parallel execution: workers={} batches={} max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={} label_representatives_per_state={} merge=origin_sink_associative fast_fail=enabled"
                , worker_count
                , batches.size()
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
                , od_day_label_retention_config.max_representatives_per_label
            )
            , LogLevel::Info
        );

        std::atomic<std::size_t> next_batch{ 0u };
        std::atomic<std::size_t> completed_batches{ 0u };
        std::atomic<std::size_t> cancelled_batches{ 0u };
        std::atomic<std::size_t> pair_count{ 0u };
        std::atomic<std::size_t> day_path_alternative_count{ 0u };
        std::atomic<std::size_t> empty_pair_count{ 0u };
        SearchCancellationToken cancellation;
        std::mutex origin_sink_mutex;
        std::vector<std::future<mathfp::Expected<mathfp::Unit>>> workers;
        workers.reserve(worker_count);

        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.push_back(
                std::async(
                      std::launch::async
                    , [&, worker]() -> mathfp::Expected<mathfp::Unit> {
                          for (;;) {
                              if (search_cancelled(&cancellation)) {
                                  return mathfp::kUnit;
                              }
                              const auto i = next_batch.fetch_add(
                                    1u
                                  , std::memory_order_relaxed
                              );
                              if (i >= batches.size()) {
                                  return mathfp::kUnit;
                              }

                              const auto& batch = batches[i];
                              if (
                                     i == 0
                                  || (i % kTaskProgressStep) == 0
                                  || (i + 1) == batches.size()
                              ) {
                                  status(
                                      fmt::format(
                                            "OD-day search: worker={} origin batch {}/{} origin={} targets={} projection_slots={} completed={} total_found={}"
                                          , worker
                                          , i + 1
                                          , batches.size()
                                          , batch.key.origin.get()
                                          , batch.completion_targets.size()
                                          , batch.projection_slots.size()
                                          , completed_batches.load(std::memory_order_relaxed)
                                          , day_path_alternative_count.load(std::memory_order_relaxed)
                                      )
                                  );
                              }

                              auto slot_results_result = search_batch_connections(
                                        batch
                                      , network
                                      , residual_reverse_graph
                                      , params
                                      , search_cost
                                      , choice_config
                                      , assignment_period
                                      , admissibility_config
                                      , effective_pruning_execution
                                      , complete_connection_dominance
                                      , execution.config.partial_retention_scope
                                      , diagnostics
                                      , i
                                      , batches.size()
                                      , nullptr
                                      , od_day_label_retention_config
                                      , &cancellation
                                  );
                              if (!slot_results_result) {
                                  request_search_cancellation(&cancellation);
                                  return mathfp::unexpected(std::move(slot_results_result.error()));
                              }
                              if (search_cancelled(&cancellation)) {
                                  cancelled_batches.fetch_add(1u, std::memory_order_relaxed);
                                  return mathfp::kUnit;
                              }
                              auto slot_results = std::move(*slot_results_result);
                              auto origin_result = materialize_origin_day_search_result(
                                    batch.key.origin
                                  , std::move(slot_results)
                              );
                              auto origin_alternatives = std::size_t{ 0u };
                              auto origin_empty_pairs = std::size_t{ 0u };
                              for (const auto& pair_result : origin_result.pair_results) {
                                  origin_alternatives += pair_result.alternatives.size();
                                  if (pair_result.alternatives.empty()) {
                                      ++origin_empty_pairs;
                                  }
                              }
                              day_path_alternative_count.fetch_add(
                                    origin_alternatives
                                  , std::memory_order_relaxed
                              );
                              empty_pair_count.fetch_add(
                                    origin_empty_pairs
                                  , std::memory_order_relaxed
                              );
                              pair_count.fetch_add(
                                    origin_result.pair_results.size()
                                  , std::memory_order_relaxed
                              );
                              {
                                  std::scoped_lock lock(origin_sink_mutex);
                                  auto sink_result = origin_sink(std::move(origin_result));
                                  if (!sink_result) {
                                      request_search_cancellation(&cancellation);
                                      return mathfp::unexpected(std::move(sink_result.error()));
                                  }
                              }
                              completed_batches.fetch_add(1u, std::memory_order_relaxed);
                          }
                      }
                )
            );
        }

        mathfp::Expected<mathfp::Unit> first_worker_error = mathfp::kUnit;
        for (auto& worker : workers) {
            auto worker_result = worker.get();
            if (!worker_result && first_worker_error) {
                request_search_cancellation(&cancellation);
                first_worker_error = mathfp::unexpected(
                    std::move(worker_result.error())
                );
            }
        }
        if (cancelled_batches.load(std::memory_order_relaxed) != 0u) {
            log(
                fmt::format(
                      "OD-day search fast-fail cancellation: cancelled_batches={}"
                    , cancelled_batches.load(std::memory_order_relaxed)
                )
                , LogLevel::Warning
            );
        }
        MATHFP_TRY(std::move(first_worker_error));
        log(
            fmt::format(
                  "OD-day by-origin search result: carrier=paper_connection_tree result=day_path_post_layer origins={:>8} pairs={:>8} empty_pairs={:>8} expected_trees={:>8} day_path_alternatives={:>8} raw_complete_connections={:>8}"
                , batches.size()
                , pair_count.load(std::memory_order_relaxed)
                , empty_pair_count.load(std::memory_order_relaxed)
                , expected_tree_count
                , day_path_alternative_count.load(std::memory_order_relaxed)
                , std::size_t{ 0u }
            )
            , LogLevel::Info
        );
        both("search: OD-day branch-and-bound done");
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::runtime
