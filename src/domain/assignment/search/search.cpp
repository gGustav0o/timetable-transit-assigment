#include "timetable/domain/assignment/search/search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <queue>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/assignment/search/branch_state.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        using PartialPruningMetrics = SearchPruningMetrics;
        using NodeMetricSet         = SearchPruningMetricSet;
        using SearchNodeKey         = SearchPruningStateKey;


        // TODO??
        struct SearchNodeKeyHash final {
            std::size_t operator()(const SearchNodeKey& key) const noexcept {
                std::size_t seed = 17u;
                seed = seed * 31u + std::hash<std::int64_t>{}(static_cast<std::int64_t>(key.physical.kind));
                seed = seed * 31u + std::hash<std::int64_t>{}(key.physical.id);
                seed = seed * 31u + std::hash<bool>{}(key.occurrence.has_value());
                if (key.occurrence.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->stop    .get());
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->position.get());
                }
                seed = seed * 31u + std::hash<bool>{}(key.transfer.last_trip.has_value());
                if (key.transfer.last_trip.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.transfer.last_trip->get());
                }
                seed = seed * 31u + std::hash<bool>{}(key.transfer.last_line.has_value());
                if (key.transfer.last_line.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.transfer.last_line->get());
                }
                return seed;
            }
        };

        /**
         * @brief Structural prefix of a connection explored by search.
         *
         * This is the trace state: current location, predecessor link and the
         * timed context needed to decide future feasible extensions.
         */
        struct SearchPartialTrace final {
            ZoneId                             origin{};
            EndpointKey                        current_physical{};
            std::optional<StopOccurrenceKey>   current_occurrence{};
            ConnectionTrace                    connection_trace{};
            std::optional<std::size_t>         parent_branch{};
            std::optional<ConnectionSegmentId> incoming_segment{};
            const ConnectionSegment*           last_timed_segment{};
            const RouteSegment*                last_timed_route_segment{};
        };

        /**
         * @brief Incremental metrics of a partial connection prefix.
         *
         * The time, transfer and fare fields are parameter-independent base
         * metrics. capacity_exposure is a separate projection over a fixed
         * exogenous load snapshot for capacity-aware search; it is not a
         * post-assignment overload assessment.
         */
        struct SearchPartialMetrics final {
            std::optional<Time> departure{};
            std::optional<Time> current_time{};
            Time                access_time{};
            Time                in_vehicle_time{};
            Time                transfer_wait_time{};
            Time                transfer_walk_time{};
            Time                egress_time{};
            TransferCount       transfers{};
            double              fare{};
            CapacityExposure    capacity_exposure{};
        };

        struct SearchBranch final {
            SearchPartialTrace   trace{};
            SearchPartialMetrics metrics{};
        };

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

        using NodeMetricMap = std::unordered_map<SearchNodeKey, NodeMetricSet, SearchNodeKeyHash>;
        using BranchArena       = std::vector<SearchBranch>;

        /**
         * @brief Retained pruning state for exactly one SearchTask.
         *
         * Dominance and approximate tolerance are meaningful only relative to
         * one OD-interval task: the state-local minima of impedance, journey
         * time and transfer count are induced by that task's destination and
         * first-boarding time domain. This object must therefore never be
         * shared between tasks, even when tasks have the same origin.
         */
        struct SearchTaskRetention final {
            SearchTaskRef task{};
            NodeMetricMap known_metrics{};
            CompleteConnectionRetention complete_connections{};
        };

        struct BatchTaskRef final {
            const SearchTask* task{};
            std::size_t       result_index{};
        };

        struct SearchBatchKey final {
            ZoneId                        origin{};
            IntervalId                    interval{};
            std::vector<SearchTimeWindow> departure_windows{};
        };

        [[nodiscard]] bool operator<(
              const SearchBatchKey& lhs
            , const SearchBatchKey& rhs
        ) noexcept {
            if (lhs.origin != rhs.origin) {
                return lhs.origin < rhs.origin;
            }
            if (lhs.interval != rhs.interval) {
                return lhs.interval < rhs.interval;
            }
            const auto common_size = std::min(
                  lhs.departure_windows.size()
                , rhs.departure_windows.size()
            );
            for (std::size_t i = 0; i < common_size; ++i) {
                if (lhs.departure_windows[i].begin.value() != rhs.departure_windows[i].begin.value()) {
                    return lhs.departure_windows[i].begin.value() < rhs.departure_windows[i].begin.value();
                }
                if (lhs.departure_windows[i].end.value() != rhs.departure_windows[i].end.value()) {
                    return lhs.departure_windows[i].end.value() < rhs.departure_windows[i].end.value();
                }
            }
            return lhs.departure_windows.size() < rhs.departure_windows.size();
        }

        struct SearchBatch final {
            SearchBatchKey            key{};
            const SearchTimeDomain*   departure_domain{};
            std::vector<BatchTaskRef> tasks{};
        };

        struct ResidualReachabilityKey final {
            EndpointKey       current_physical{};
            SearchBranchPhase phase{ SearchBranchPhase::BeforeFirstBoarding };
            TransferCount     remaining_transfers{};
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
            TransferCount transfers{};
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

        [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
              const SearchBranch&   branch
            , const SearchTask&     task
            , const TransferLimits& limits
        ) noexcept;

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

        [[nodiscard]] bool has_more_budget_state(
              const DestinationResidualReachability& destination
            , const ResidualReachabilityKey&         state
            , TransferCount                          max_transfers
        ) noexcept {
            for (
                auto remaining = state.remaining_transfers.get() + 1;
                remaining <= max_transfers.get();
                ++remaining
            ) {
                if (has_reachable_state(
                      destination
                    , ResidualReachabilityKey{
                          .current_physical    = state.current_physical
                        , .phase               = state.phase
                        , .remaining_transfers = TransferCount{ remaining }
                      }
                )) {
                    return true;
                }
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

            if (state.phase == SearchBranchPhase::AfterTimedRide) {
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
                            , .phase               = SearchBranchPhase::BeforeFirstBoarding
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
                if (state.remaining_transfers < max_transfers) {
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTimedRide
                            , .remaining_transfers = TransferCount{
                                  state.remaining_transfers.get() + 1
                              }
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
                } else if (state.phase == SearchBranchPhase::AfterTimedRide) {
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
                                  , .phase               = SearchBranchPhase::BeforeFirstBoarding
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

                if (state.remaining_transfers < max_transfers) {
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTimedRide
                              , .remaining_transfers = TransferCount{
                                    state.remaining_transfers.get() + 1
                                }
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
            for (std::int32_t remaining = 0; remaining <= max_transfers.get(); ++remaining) {
                const auto seed = ResidualReachabilityKey{
                      .current_physical    = destination_endpoint
                    , .phase               = SearchBranchPhase::Completed
                    , .remaining_transfers = TransferCount{ remaining }
                };
                distances.emplace(seed, 0.0);
                frontier.push(ResidualDistanceQueueItem{
                      .distance = 0.0
                    , .state    = seed
                });
            }

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

            for (std::int32_t remaining = 0; remaining <= max_transfers.get(); ++remaining) {
                enqueue_reachable_state(
                      reachability
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = destination_endpoint
                        , .phase               = SearchBranchPhase::Completed
                        , .remaining_transfers = TransferCount{ remaining }
                      }
                );
            }

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
            , std::span<const BatchTaskRef> tasks
            , TransferCount               max_transfers
            , const SearchImpedance&      impedance
            , double                      fare_scale
        ) {
            ResidualReachability reachability;

            for (const auto& task_ref : tasks) {
                const auto destination = task_ref.task->destination;
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

            for (std::int32_t remaining = 0; remaining <= max_transfers.get(); ++remaining) {
                if (!has_reachable_state(
                      reachability
                    , ResidualReachabilityKey{
                          .current_physical    = destination_endpoint
                        , .phase               = SearchBranchPhase::Completed
                        , .remaining_transfers = TransferCount{ remaining }
                      }
                )) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability misses destination completion seed")
                            .ctx("destination", destination.get())
                            .ctx("remaining_transfers", remaining)
                    );
                }
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

                if (state.remaining_transfers < max_transfers) {
                    const auto relaxed_more_budget_state = ResidualReachabilityKey{
                          .current_physical    = state.current_physical
                        , .phase               = state.phase
                        , .remaining_transfers = TransferCount{
                              state.remaining_transfers.get() + 1
                          }
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

        [[nodiscard]] Time partial_journey_time(
            const SearchPartialMetrics& metrics
        ) noexcept {
            return Time{
                metrics.current_time->value() - metrics.departure->value()
            };
        }

        [[nodiscard]] Time partial_walk_time(
            const SearchPartialMetrics& metrics
        ) noexcept {
            return metrics.access_time + metrics.transfer_walk_time + metrics.egress_time;
        }

        [[nodiscard]] ConnectionImpedanceComponents partial_impedance_components(
            const SearchPartialMetrics& metrics
        ) noexcept {
            return ConnectionImpedanceComponents{
                  .in_vehicle_time    = metrics.in_vehicle_time
                , .access_time        = metrics.access_time
                , .egress_time        = metrics.egress_time
                , .transfer_walk_time = metrics.transfer_walk_time
                , .transfer_wait_time = metrics.transfer_wait_time
                , .transfer_count     = metrics.transfers
                , .fare               = metrics.fare
            };
        }

        SearchPruningTransferContext search_transfer_context(
            const SearchBranch& branch
        ) noexcept {
            return SearchPruningTransferContext{
                  .last_trip = branch.trace.last_timed_segment != nullptr
                    ? branch.trace.last_timed_segment->trip
                    : std::optional<TripId>{}
                , .last_line = branch.trace.last_timed_route_segment != nullptr
                    ? line_of(*branch.trace.last_timed_route_segment)
                    : std::optional<LineId>{}
            };
        }

        std::optional<StopOccurrenceKey> search_last_timed_occurrence(
            const SearchBranch& branch
        ) noexcept {
            if (branch.trace.last_timed_route_segment == nullptr) {
                return std::nullopt;
            }
            return occurrence_key(
                line_topology_of(*branch.trace.last_timed_route_segment)->to
            );
        }

        SearchPruningStateProjection search_pruning_state_projection(
            const SearchBranch& branch
        ) noexcept {
            return SearchPruningStateProjection{
                  .physical              = branch.trace.current_physical
                , .current_occurrence    = branch.trace.current_occurrence
                , .last_timed_occurrence = search_last_timed_occurrence(branch)
                , .transfer              = search_transfer_context(branch)
            };
        }

        SearchNodeKey search_node_key(
              const SearchBranch&               branch
            , const SearchPruningExecutionPlan& pruning_execution
        ) noexcept {
            return make_search_pruning_state_key(
                  search_pruning_state_projection(branch)
                , pruning_execution.equivalent_connection_dominance
            );
        }

        bool branch_revisits_physical(
              const BranchArena&  branches
            , const SearchBranch& branch
            , EndpointKey         next
        ) {
            if (branch.trace.current_physical == next) {
                return true;
            }

            auto cursor = branch.trace.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branches[*cursor];
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
            , StopOccurrenceKey   next
        ) {
            if (branch.trace.current_occurrence.has_value() && branch.trace.current_occurrence.value() == next) {
                return true;
            }

            auto cursor = branch.trace.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branches[*cursor];
                if (ancestor.trace.current_occurrence.has_value() && ancestor.trace.current_occurrence.value() == next) {
                    return true;
                }
                cursor = ancestor.trace.parent_branch;
            }

            return false;
        }

        bool is_complete_connection(
              const SearchBranch& branch
            , ZoneId              task_destination
        ) noexcept {
            return branch.metrics.departure.has_value()
                && branch.trace.current_physical.kind == EndpointKind::Zone
                && branch.trace.current_physical.id   == task_destination.get();
        }

        [[nodiscard]] SearchBranchPhase search_branch_phase(
              const SearchBranch& branch
            , ZoneId              task_destination
        ) noexcept {
            if (is_complete_connection(branch, task_destination)) {
                return SearchBranchPhase::Completed;
            }
            return branch.metrics.departure.has_value()
                ? SearchBranchPhase::AfterTimedRide
                : SearchBranchPhase::BeforeFirstBoarding;
        }

        [[nodiscard]] TransferCount remaining_transfer_budget(
              const SearchBranch&   branch
            , const TransferLimits& limits
        ) noexcept {
            const auto used = branch.metrics.departure.has_value()
                ? branch.metrics.transfers.get()
                : 0;
            return TransferCount{
                std::max(0, limits.max_transfers.get() - used)
            };
        }

        [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
              const SearchBranch&   branch
            , const SearchTask&     task
            , const TransferLimits& limits
        ) noexcept {
            return RelaxedSuffixState{
                  .current_physical    = branch.trace.current_physical
                , .destination         = task.destination
                , .phase               = search_branch_phase(branch, task.destination)
                , .remaining_transfers = remaining_transfer_budget(branch, limits)
            };
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
              const SearchBranch&    branch
            , const SearchCostContext& search_cost
        ) {
            const auto journey_time = partial_journey_time(branch.metrics);
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(branch.metrics)
                , .capacity_exposure = branch.metrics.capacity_exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(cost_components, search_cost)
            );

            return PartialPruningMetrics{
                  .departure    = *branch.metrics.departure
                , .arrival      = *branch.metrics.current_time
                , .journey_time = journey_time
                , .walk_time    = partial_walk_time(branch.metrics)
                , .transfers    = branch.metrics.transfers
                , .fare         = branch.metrics.fare
                , .impedance    = impedance
            };
        }

        void insert_pruning_metrics(
              SearchTaskRetention&              retention
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchNodeKey                     node
            , PartialPruningMetrics             metrics
        ) {
            auto& known = retention.known_metrics[node];
            known = insert_search_pruning_metrics(
                  pruning_execution
                , std::move(known)
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
            , const SearchTask&                  task
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

            const auto destination_it = reachability.destinations.find(task.destination);
            if (destination_it == reachability.destinations.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto state = residual_reachability_key(
                relaxed_suffix_state(branch, task, params.transfers)
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
            if (current_line->to.stop != successor_line->from.stop) {
                return false;
            }
            if (!branch.trace.last_timed_segment->to_index.has_value() || !successor.from_index.has_value()) {
                return false;
            }
            return successor.from_index.value() < branch.trace.last_timed_segment->to_index.value();
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
                if (!same_line(*branch.trace.last_timed_route_segment, continuation_route_segment)) {
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

        bool is_active_batch_destination(
              EndpointKey                       endpoint
            , std::span<const std::size_t>      active_tasks
            , std::span<const BatchTaskRef>     batch_tasks
        ) noexcept {
            if (endpoint.kind != EndpointKind::Zone) {
                return false;
            }
            for (const auto task_pos : active_tasks) {
                if (batch_tasks[task_pos].task->destination.get() == endpoint.id) {
                    return true;
                }
            }
            return false;
        }

        bool is_batch_admissible_walk_successor(
              ZoneId                         origin
            , std::span<const std::size_t>   active_tasks
            , std::span<const BatchTaskRef>  batch_tasks
            , const SearchBranch&            branch
            , const RouteSegment&            route_segment
        ) noexcept {
            const auto next_physical = physical_to_key(route_segment);
            const auto before_first_timed_boarding = !branch.metrics.departure.has_value();

            if (before_first_timed_boarding) {
                if (branch.trace.current_physical.kind != EndpointKind::Zone
                    || branch.trace.current_physical.id != origin.get()) {
                    return false;
                }
                // In the paper's connection tree, access is one connection
                // segment from the origin centroid to the first boarding stop.
                return next_physical.kind == EndpointKind::Stop;
            }

            if (branch.trace.current_physical.kind == EndpointKind::Zone) {
                return false;
            }
            if (next_physical.kind == EndpointKind::Zone) {
                return is_active_batch_destination(next_physical, active_tasks, batch_tasks);
            }
            return true;
        }

        [[nodiscard]] Time add_time(
              Time lhs
            , Time rhs
        ) noexcept {
            return Time{ lhs.value() + rhs.value() };
        }

        [[nodiscard]] Time duration_of(
            const ConnectionLeg& leg
        ) noexcept {
            return Time{ leg.end_time.value() - leg.start_time.value() };
        }

        [[nodiscard]] Time next_relative_access_start(
            const ConnectionTrace& trace
        ) noexcept {
            if (trace.legs.empty()) {
                return Time{ 0.0 };
            }
            return trace.legs.back().end_time;
        }

        [[nodiscard]] ConnectionTrace anchor_access_trace(
              ConnectionTrace trace
            , Time            departure
        ) {
            auto cursor = departure;
            for (auto& leg : trace.legs) {
                const auto duration = duration_of(leg);
                leg.start_time = cursor;
                leg.end_time   = add_time(cursor, duration);
                cursor         = leg.end_time;
            }
            return trace;
        }

        [[nodiscard]] ConnectionLegKind classify_walk_extension(
              const SearchPartialMetrics& metrics
            , EndpointKey                  next_physical
        ) noexcept {
            if (!metrics.departure.has_value()) {
                return ConnectionLegKind::AccessWalk;
            }
            if (next_physical.kind == EndpointKind::Zone) {
                return ConnectionLegKind::EgressWalk;
            }
            return ConnectionLegKind::TransferWalk;
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
              const SearchPartialMetrics& metrics
            , const ConnectionSegment&    segment
            , EndpointKey                  physical
        ) noexcept {
            if (!metrics.current_time.has_value() || !segment.departure.has_value()) {
                return std::nullopt;
            }
            if (segment.departure->value() <= metrics.current_time->value()) {
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
                , .start_time         = *metrics.current_time
                , .end_time           = *segment.departure
                , .length             = Length{ 0.0 }
                , .fare               = 0.0
            };
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

        [[nodiscard]] SearchPartialTrace extend_trace_with_walk(
              SearchPartialTrace trace
            , std::size_t         branch_index
            , const SearchPartialMetrics& metrics
            , const RouteSegment& route_segment
            , ConnectionSegmentId segment_id
        ) {
            const auto next_physical = physical_to_key(route_segment);
            const auto kind          = classify_walk_extension(metrics, next_physical);
            const auto start_time    = metrics.current_time.has_value()
                ? *metrics.current_time
                : next_relative_access_start(trace.connection_trace);

            trace.connection_trace.legs.push_back(
                make_walk_leg(kind, segment_id, route_segment, start_time)
            );
            trace.parent_branch      = branch_index;
            trace.incoming_segment   = segment_id;
            trace.current_physical   = next_physical;
            trace.current_occurrence = std::nullopt;
            return trace;
        }

        [[nodiscard]] SearchPartialMetrics extend_metrics_with_walk(
              SearchPartialMetrics metrics
            , const RouteSegment&   route_segment
            , EndpointKey           next_physical
        ) {
            if (metrics.departure.has_value()) {
                metrics.current_time = Time{
                    metrics.current_time->value() + route_segment.run_time.value()
                };
                if (next_physical.kind == EndpointKind::Stop) {
                    metrics.transfer_walk_time = Time{
                        metrics.transfer_walk_time.value() + route_segment.run_time.value()
                    };
                } else {
                    metrics.egress_time = Time{
                        metrics.egress_time.value() + route_segment.run_time.value()
                    };
                }
            } else {
                metrics.access_time = Time{
                    metrics.access_time.value() + route_segment.run_time.value()
                };
            }

            return metrics;
        }

        [[nodiscard]] SearchBranch extend_with_walk(
              const SearchBranch& branch
            , std::size_t         branch_index
            , const RouteSegment& route_segment
            , ConnectionSegmentId segment_id
        ) {
            const auto trace = extend_trace_with_walk(
                  branch.trace
                , branch_index
                , branch.metrics
                , route_segment
                , segment_id
            );
            return SearchBranch{
                  .trace   = trace
                , .metrics = extend_metrics_with_walk(
                      branch.metrics
                    , route_segment
                    , trace.current_physical
                  )
            };
        }

        [[nodiscard]] SearchPartialTrace extend_trace_with_timed(
              SearchPartialTrace       trace
            , std::size_t               branch_index
            , const SearchPartialMetrics& metrics
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
        ) {
            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            if (!metrics.departure.has_value()) {
                trace.connection_trace = anchor_access_trace(
                      std::move(trace.connection_trace)
                    , Time{ segment.departure->value() - metrics.access_time.value() }
                );
            } else if (auto wait_leg = make_transfer_wait_leg(
                  metrics
                , segment
                , trace.current_physical
            )) {
                trace.connection_trace.legs.push_back(*wait_leg);
            }
            trace.connection_trace.legs.push_back(make_ride_leg(segment, route_segment));
            trace.parent_branch                  = branch_index;
            trace.incoming_segment               = segment.id;
            trace.current_physical               = physical_to_key(route_segment);
            trace.current_occurrence             = next_occurrence;
            trace.last_timed_segment             = &segment;
            trace.last_timed_route_segment       = &route_segment;
            return trace;
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
            , IntervalId               interval
            , const SearchCostContext& search_cost
        ) {
            switch (search_cost.mode) {
                case SearchCostMode::BaseOnly:
                    return CapacityExposure{ Time{ 0.0 } };

                case SearchCostMode::CapacityAware:
                    return search_capacity_exposure(
                          make_ride_leg(segment, route_segment)
                        , interval
                        , search_cost.capacity
                    );
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("unknown search cost mode")
                    .ctx("mode", static_cast<std::int64_t>(search_cost.mode))
            );
        }

        [[nodiscard]] SearchPartialMetrics extend_metrics_with_timed(
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
                metrics.transfers = TransferCount{ metrics.transfers.get() + 1 };
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
            , IntervalId               interval
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
            return SearchBranch{
                  .trace   = extend_trace_with_timed(
                        branch.trace
                      , branch_index
                      , branch.metrics
                      , segment
                      , route_segment
                  )
                , .metrics = extend_metrics_with_timed(
                      branch.metrics
                    , segment
                    , capacity_exposure
                  )
            };
        }

        mathfp::Expected<std::optional<SearchBranch>> extend_branch(
              const BranchArena&         branches
            , std::size_t                branch_index
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , IntervalId                  interval
            , const SearchCostContext&    search_cost
        ) {
            const auto& route_segment = route_segment_at(network, successor.route_segment);
            if (is_walk_connection(successor)) {
                const auto next_physical = physical_to_key(route_segment);
                if (branch_revisits_physical(branches, branch, next_physical)) {
                    return std::optional<SearchBranch>{};
                }
                return std::optional<SearchBranch>{
                    extend_with_walk(branch, branch_index, route_segment, successor.id)
                };
            }

            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            if (branch_revisits_occurrence(branches, branch, next_occurrence)) {
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
                    , interval
                    , search_cost
                )
            );
            return std::optional<SearchBranch>{ std::move(extended) };
        }

        template <typename Visitor>
        void for_each_successor(
              const PreprocessedNetwork& network
            , ZoneId                      origin
            , std::span<const std::size_t> active_tasks
            , std::span<const BatchTaskRef> batch_tasks
            , const SearchBranch&        branch
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
            , Visitor&&                  visit
        ) {
            auto&& visitor = visit;
            const auto lookup = preprocessing::lookup_from(
                  network.route_index
                , network.connection_index
                , branch.trace.current_physical
            );

            for (const auto connection_id : lookup.walk_connections) {
                const auto& connection = connection_segment_at(network, connection_id);
                const auto& route_segment = route_segment_at(network, connection.route_segment);
                if (is_batch_admissible_walk_successor(
                      origin
                    , active_tasks
                    , batch_tasks
                    , branch
                    , route_segment
                )) {
                    visitor(connection_id);
                }
            }

            for_each_timed_successor(
                  network
                , branch.trace.current_physical
                , branch.metrics.current_time
                , limits
                , first_departure_domain
                , visitor
            );
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

        mathfp::Expected<std::optional<SearchConnection>> complete_connection(
              const SearchBranch&        branch
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
                  SearchConnection
                , connection
                , make_search_connection(
                      branch.trace.origin
                    , destination
                    , branch.trace.connection_trace
                )
            );
            return std::optional<SearchConnection>{ std::move(connection) };
        }

        mathfp::Expected<SearchPruningDecision> retain_branch(
              const SearchBranch&               branch
            , SearchTaskRetention&              retention
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
            auto it          = retention.known_metrics.find(node);
            if (it == retention.known_metrics.end()) {
                if (stores_search_pruning_metrics(pruning_execution)) {
                    insert_pruning_metrics(retention, pruning_execution, node, std::move(metrics));
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

            const auto decision = evaluate_search_pruning(
                  pruning_execution
                , metrics
                , it->second
                , params.transfers
            );
            if (!decision.accepted) {
                if (decision.layer == SearchPruningLayer::Exact) {
                    ++pruning_stats.rejected_exact;
                } else {
                    ++pruning_stats.rejected_approximate;
                }
                return decision;
            }

            if (stores_search_pruning_metrics(pruning_execution)) {
                insert_pruning_metrics(retention, pruning_execution, node, std::move(metrics));
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return decision;
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

        [[nodiscard]] std::size_t retained_connection_count(
            std::span<const SearchTaskRetention> retentions
        ) noexcept {
            std::size_t total = 0;
            for (const auto& retention : retentions) {
                total += retention.complete_connections.alternatives.size();
            }
            return total;
        }

        struct ReachabilityTaskFilter final {
            std::vector<std::size_t> reachable{};
            std::vector<RejectedReachabilityTask> unreachable{};
        };

        [[nodiscard]] std::vector<std::size_t> all_batch_task_positions(
            std::span<const BatchTaskRef> tasks
        ) {
            std::vector<std::size_t> positions;
            positions.reserve(tasks.size());
            for (std::size_t i = 0; i < tasks.size(); ++i) {
                positions.push_back(i);
            }
            return positions;
        }

        [[nodiscard]] ReachabilityTaskFilter filter_task_positions_by_reachability(
              std::span<const std::size_t>  task_positions
            , std::span<const BatchTaskRef> tasks
            , const ResidualReachability&   reachability
            , const SearchBranch&           branch
            , const TransferLimits&         limits
        ) {
            ReachabilityTaskFilter result;
            result.reachable  .reserve(task_positions.size());
            result.unreachable.reserve(task_positions.size());
            for (const auto task_pos : task_positions) {
                const auto decision = evaluate_residual_reachability(
                      reachability
                    , relaxed_suffix_state(branch, *tasks[task_pos].task, limits)
                    , limits.max_transfers
                );
                if (decision.feasible) {
                    result.reachable.push_back(task_pos);
                } else {
                    result.unreachable.push_back(
                        RejectedReachabilityTask{
                              .task_position = task_pos
                            , .reason        = decision.rejection_reason
                        }
                    );
                }
            }
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
            , std::span<const std::size_t>      active_tasks
            , std::span<const BatchTaskRef>     batch_tasks
        ) {
            std::vector<std::size_t> matches;
            if (!branch.metrics.departure.has_value()
                || branch.trace.current_physical.kind != EndpointKind::Zone) {
                return matches;
            }

            for (const auto task_pos : active_tasks) {
                if (batch_tasks[task_pos].task->destination.get() == branch.trace.current_physical.id) {
                    matches.push_back(task_pos);
                }
            }
            return matches;
        }

        mathfp::Expected<mathfp::Unit> retain_complete_for_task(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const SearchParams&        params
            , const SearchCostContext&   search_cost
            , const SearchTask&          task
            , const AssignmentPeriodConfig& assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
            , const CompleteConnectionDominanceConfig& dominance_config
            , SearchTaskRetention&       retention
            , TaskSearchStats&           stats
        ) {
            MATHFP_TRY_LET(
                  std::optional<SearchConnection>
                , complete
                , complete_connection(branch, network, params.transfers, task.destination)
            );
            if (complete.has_value()) {
                ++stats.completed_connections;
                if (!connection_admissible_for_assignment_period(
                      metrics_of(*complete)
                    , task.interval
                    , assignment_period
                    , admissibility_config
                )) {
                    ++stats.rejected_complete_admissibility;
                    return mathfp::kUnit;
                }
                const auto retention_decision = retain_exact_complete_connection(
                      retention.complete_connections
                    , std::move(*complete)
                    , search_cost
                    , task.interval.id
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

        mathfp::Expected<mathfp::Unit> search_batch_connections(
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
            , SearchDiagnosticsContext          diagnostics
            , std::size_t                       batch_index
            , std::size_t                       batch_count
            , std::vector<SearchTaskResult>&    result_slots
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            std::vector<SearchTaskRetention> retentions;
            retentions.reserve(batch.tasks.size());
            for (const auto& task_ref : batch.tasks) {
                retentions.push_back(SearchTaskRetention{ .task = task_ref.task->index });
            }

            TaskSearchStats                   stats;
            std::vector<TaskSearchStats>      task_stats(batch.tasks.size());
            BranchArena                       branches;
            std::vector<std::vector<std::size_t>> branch_active_tasks;
            const SearchTimeDomain*           first_departure_domain = batch.departure_domain;
            const auto                        batch_task_span =
                std::span<const BatchTaskRef>{ batch.tasks.data(), batch.tasks.size() };
            const auto                        reachability = build_residual_reachability(
                  reverse_graph
                , batch_task_span
                , params.transfers.max_transfers
                , search_cost.impedance
                , search_cost.fare_scale
            );
            MATHFP_TRY(validate_residual_reachability(
                  reachability
                , params.transfers.max_transfers
            ));

            branches.reserve(kInitialTaskBranchReserve);
            branch_active_tasks.reserve(kInitialTaskBranchReserve);

            std::deque<std::size_t> current_frontier;
            std::deque<std::size_t> next_frontier;
            branches.push_back(
                SearchBranch{
                      .trace = SearchPartialTrace{
                            .origin                   = batch.key.origin
                          , .current_physical         = endpoint_key(batch.key.origin)
                          , .current_occurrence       = std::nullopt
                          , .connection_trace         = ConnectionTrace{}
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
                }
            );
            const auto root_task_positions = all_batch_task_positions(batch_task_span);
            auto root_reachability = filter_task_positions_by_reachability(
                  std::span<const std::size_t>{
                      root_task_positions.data()
                    , root_task_positions.size()
                  }
                , batch_task_span
                , reachability
                , branches.back()
                , params.transfers
            );
            record_reachability_rejections(
                  std::span<const RejectedReachabilityTask>{
                      root_reachability.unreachable.data()
                    , root_reachability.unreachable.size()
                  }
                , task_stats
                , stats
            );
            branch_active_tasks.push_back(std::move(root_reachability.reachable));
            current_frontier.push_back(0);

            status(
                fmt::format(
                      "search: batch {}/{} origin={} interval={} tasks={} capacity_iteration={} frontier={} found={}"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , batch.key.interval.get()
                    , batch.tasks.size()
                    , diagnostics.capacity_iteration
                    , current_frontier.size()
                    , retained_connection_count(retentions)
                )
            );

            using Clock = std::chrono::steady_clock;
            const auto batch_started_at = Clock::now();
            auto last_wall_clock_heartbeat = batch_started_at;
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
                              " tasks={:>5} capacity_iteration={:>4} stage={} branch={} elapsed_ms={}"
                              " expanded={:>8} generated={:>8} accepted={:>8} found={:>8}"
                              " frontier={}/{}"
                            , static_cast<std::int64_t>(batch_index)
                            , batch.key.origin.get()
                            , batch.key.interval.get()
                            , batch.tasks.size()
                            , diagnostics.capacity_iteration
                            , stage
                            , static_cast<std::int64_t>(branch_index)
                            , static_cast<std::int64_t>(elapsed_ms)
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , retained_connection_count(retentions)
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                        , LogLevel::Info
                    );
                };

            while (!current_frontier.empty() || !next_frontier.empty()) {
                if (current_frontier.empty()) {
                    current_frontier.swap(next_frontier);
                }

                stats.max_current_frontier = std::max(stats.max_current_frontier, current_frontier.size());
                stats.max_next_frontier    = std::max(stats.max_next_frontier   , next_frontier   .size());

                const auto branch_index = current_frontier.front();
                current_frontier.pop_front();
                emit_wall_clock_heartbeat("branch", branch_index);
                const auto branch = branches[branch_index];
                const auto active_tasks = std::span<const std::size_t>{
                      branch_active_tasks[branch_index].data()
                    , branch_active_tasks[branch_index].size()
                };
                ++stats.expanded_branches;

                if ((stats.expanded_branches % kSearchHeartbeatStep) == 0) {
                    status(
                        fmt::format(
                              "search: batch {}/{} origin={} interval={} tasks={} capacity_iteration={} expanded={} accepted={} found={} frontier={}/{}"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , batch.key.interval.get()
                            , batch.tasks.size()
                            , diagnostics.capacity_iteration
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_connection_count(retentions)
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                    );
                    log(
                        fmt::format(
                              "search heartbeat: batch={:>8} origin={:>4} interval={:>4} tasks={:>5} capacity_iteration={:>4} expanded={:>8} generated={:>8}"
                              " accepted={:>8} found={:>8} rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                              " lower_bound_pruned={}"
                              " pruning(exact/approx/inserted/skipped)={}/{}/{}/{}"
                              " frontier={}/{}"
                            , static_cast<std::int64_t>(batch_index)
                            , batch.key.origin.get()
                            , batch.key.interval.get()
                            , batch.tasks.size()
                            , diagnostics.capacity_iteration
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , retained_connection_count(retentions)
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
                        )
                        , LogLevel::Info
                    );
                    log(
                        format_search_pruning_runtime_stats(summarize(stats.pruning))
                        , LogLevel::Info
                    );
                }

                if (active_tasks.empty()
                    || (branch.trace.current_physical.kind == EndpointKind::Zone
                        && branch.metrics.departure.has_value())) {
                    continue;
                }

                mathfp::Expected<mathfp::Unit> successor_error = mathfp::kUnit;
                for_each_successor(
                      network
                    , batch.key.origin
                    , active_tasks
                    , batch_task_span
                    , branch
                    , params.transfers
                    , first_departure_domain
                    , [&](ConnectionSegmentId successor_id) {
                    if (!successor_error) {
                        return;
                    }
                    ++stats.generated_successors;
                    if ((stats.generated_successors % kSearchWallClockSuccessorCheckStep) == 0) {
                        emit_wall_clock_heartbeat("successor", branch_index);
                    }
                    const auto& successor               = connection_segment_at(network, successor_id);
                    const auto& successor_route_segment = route_segment_at(
                          network
                        , successor.route_segment
                    );
                    if (!first_timed_departure_allowed(
                          branch
                        , successor
                        , first_departure_domain
                        , params.transfers
                    )) {
                        ++stats.rejected_time_domain;
                        return;
                    }
                    if (!is_branch_extension_feasible(
                          feasibility_state(branch)
                        , successor
                        , successor_route_segment
                        , params.transfers
                    )) {
                        ++stats.rejected_feasibility;
                        return;
                    }
                    if (!improves_repeated_stop_reboarding(
                          branch
                        , network
                        , successor
                        , successor_route_segment
                    )) {
                        ++stats.rejected_reboarding;
                        return;
                    }

                    auto candidate_result = extend_branch(
                          branches
                        , branch_index
                        , branch
                        , network
                        , successor
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

                    if (candidate->metrics.transfers > params.transfers.max_transfers) {
                        ++stats.rejected_transfer_limit;
                        return;
                    }

                    const auto complete_task_positions = matching_complete_tasks(
                          *candidate
                        , active_tasks
                        , batch_task_span
                    );
                    if (!complete_task_positions.empty()) {
                        for (const auto task_pos : complete_task_positions) {
                            const auto completed_before = task_stats[task_pos].completed_connections;
                            auto complete_result = retain_complete_for_task(
                                  *candidate
                                , network
                                , params
                                , search_cost
                                , *batch.tasks[task_pos].task
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
                        }
                        return;
                    }

                    if (candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }

                    const auto reachable_tasks = filter_task_positions_by_reachability(
                          active_tasks
                        , batch_task_span
                        , reachability
                        , *candidate
                        , params.transfers
                    );
                    record_reachability_rejections(
                          std::span<const RejectedReachabilityTask>{
                              reachable_tasks.unreachable.data()
                            , reachable_tasks.unreachable.size()
                          }
                        , task_stats
                        , stats
                    );
                    if (reachable_tasks.reachable.empty()) {
                        return;
                    }

                    std::vector<std::size_t> next_active_tasks;
                    next_active_tasks.reserve(reachable_tasks.reachable.size());
                    std::vector<RejectedSuffixLowerBoundTask> lower_bound_rejected_tasks;
                    lower_bound_rejected_tasks.reserve(reachable_tasks.reachable.size());
                    for (const auto task_pos : reachable_tasks.reachable) {
                        auto lower_bound_decision = evaluate_suffix_lower_bound_pruning(
                              *candidate
                            , *batch.tasks[task_pos].task
                            , reachability
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
                            continue;
                        }

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
                            continue;
                        }
                        next_active_tasks.push_back(task_pos);
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
                    const auto same_level =
                        is_walk_connection(successor) || !branch.metrics.departure.has_value();
                    branches.push_back(std::move(*candidate));
                    branch_active_tasks.push_back(std::move(next_active_tasks));
                    const auto candidate_index = branches.size() - 1;
                    if (same_level) {
                        current_frontier.push_back(candidate_index);
                    } else {
                        next_frontier   .push_back(candidate_index);
                    }
                });
                MATHFP_TRY(std::move(successor_error));
            }

            std::size_t batch_final_found = 0;
            std::size_t batch_retained_before_tolerance = 0;
            for (std::size_t task_pos = 0; task_pos < batch.tasks.size(); ++task_pos) {
                auto& task_result = result_slots[batch.tasks[task_pos].result_index];
                auto& retention   = retentions[task_pos];
                const auto before_tolerance = retention.complete_connections.alternatives.size();
                task_result.connections = finalize_complete_connection_retention(
                      retention.complete_connections
                    , params.choice_tolerances
                    , choice_config.rollout_stage
                );
                task_stats[task_pos].rejected_complete_tolerance =
                    before_tolerance - task_result.connections.size();
                stats.rejected_complete_admissibility += task_stats[task_pos].rejected_complete_admissibility;
                stats.rejected_complete_dominance += task_stats[task_pos].rejected_complete_dominance;
                stats.removed_complete_dominated  += task_stats[task_pos].removed_complete_dominated;
                stats.rejected_complete_tolerance += task_stats[task_pos].rejected_complete_tolerance;
                batch_final_found += task_result.connections.size();
                batch_retained_before_tolerance += before_tolerance;
                MATHFP_TRY(validate_reachability_rejection_stats(task_stats[task_pos]));
                MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(task_stats[task_pos]));

                log(
                    fmt::format(
                          "search batch task done: batch={}/{} task={} origin={} destination={} interval={} found={:>8}"
                          " retained_complete={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                          " reachability_pruned={} reachability_detail(phase/budget/unreachable)={}/{}/{}"
                          " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                        , batch_index + 1
                        , batch_count
                        , task_result.task.index.get()
                        , task_result.task.origin.get()
                        , task_result.task.destination.get()
                        , task_result.task.interval.id.get()
                        , task_result.connections.size()
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
            MATHFP_TRY(validate_reachability_rejection_stats(stats));
            MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(stats));

            log(
                fmt::format(
                      "search batch done: {}/{} origin={} interval={} tasks={} found={:>8} completed={:>8}"
                      " retained_complete={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                      " expanded={:>8} generated={:>8} accepted={:>8}"
                      " rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                      " reachability_detail(phase/budget/unreachable)={}/{}/{} max_frontier={}/{}"
                      " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , batch.key.interval.get()
                    , batch.tasks.size()
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
                )
                , LogLevel::Info
            );
            log(
                format_search_pruning_runtime_stats(summarize(stats.pruning))
                , LogLevel::Info
            );

            return mathfp::kUnit;
        }

    }  // namespace

    SearchConnection::SearchConnection(
        Connection connection
    )
        : connection_(std::move(connection)) {}

    mathfp::Expected<std::vector<SearchTask>> build_search_tasks(
        const InputModel& input
    ) {
        auto intervals_result = interval_lookup(input);
        if (!intervals_result) {
            return mathfp::unexpected(std::move(intervals_result.error()));
        }
        auto intervals = std::move(*intervals_result);
        const auto task_departure_padding = SearchTimePadding{};
        std::vector<SearchTask> tasks;
        tasks.reserve(input.demand.size());

        for (const auto& demand : input.demand) {
            if (!(demand.passengers > 0.0)) {
                continue;
            }

            const auto interval_it = intervals.find(demand.interval);
            if (interval_it == intervals.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search task demand entry references unknown interval")
                        .ctx("origin"     , demand.origin     .get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval   .get())
                );
            }

            MATHFP_TRY_LET(
                  SearchTimeDomain
                , departure_domain
                , make_search_time_domain(std::vector<SearchTimeWindow>{
                    expand_interval_to_search_window(
                          *interval_it->second
                        , task_departure_padding
                    )
                })
            );

            tasks.push_back(
                SearchTask{
                      .index            = SearchTaskRef{ static_cast<std::int64_t>(tasks.size()) }
                    , .origin           = demand.origin
                    , .destination      = demand.destination
                    , .interval         = *interval_it->second
                    , .departure_domain = std::move(departure_domain)
                }
            );
        }

        return tasks;
    }

    namespace {

        std::vector<SearchBatch> build_search_batches(
            std::span<const SearchTask> tasks
        ) {
            std::vector<SearchBatch> batches;
            std::map<SearchBatchKey, std::size_t> index_by_key;

            for (std::size_t i = 0; i < tasks.size(); ++i) {
                const auto& task = tasks[i];
                SearchBatchKey key{
                      .origin            = task.origin
                    , .interval          = task.interval.id
                    , .departure_windows = task.departure_domain.windows
                };

                const auto [it, inserted] = index_by_key.emplace(key, batches.size());
                if (inserted) {
                    batches.push_back(
                        SearchBatch{
                              .key              = std::move(key)
                            , .departure_domain = &task.departure_domain
                            , .tasks            = {}
                        }
                    );
                }

                batches[it->second].tasks.push_back(
                    BatchTaskRef{
                          .task         = &task
                        , .result_index = i
                    }
                );
            }

            return batches;
        }

    }  // namespace

    std::vector<const SearchConnection*> search_connection_ptrs(
        const ConnectionSearchResult& result
    ) {
        std::vector<const SearchConnection*> connections;
        const auto total = search_connection_count(result);
        connections.reserve(total);
        for (const auto& task_result : result.task_results) {
            for (const auto& connection : task_result.connections) {
                connections.push_back(&connection);
            }
        }
        return connections;
    }

    std::size_t search_connection_count(
        const ConnectionSearchResult& result
    ) noexcept {
        std::size_t total = 0;
        for (const auto& task_result : result.task_results) {
            total += task_result.connections.size();
        }
        return total;
    }

    mathfp::Expected<SearchConnection> make_search_connection(
        Connection connection
    ) {
        return make_search_connection(
              connection.origin
            , connection.destination
            , std::move(connection.trace)
        );
    }

    mathfp::Expected<SearchConnection> make_search_connection(
          ZoneId          origin
        , ZoneId          destination
        , ConnectionTrace trace
    ) {
        MATHFP_TRY_LET(
              Connection
            , connection
            , make_connection(origin, destination, std::move(trace))
        );
        return SearchConnection{ std::move(connection) };
    }

    const Connection& canonical_connection(
        const SearchConnection& connection
    ) noexcept {
        return connection.connection_;
    }

    ZoneId origin_of(
        const SearchConnection& connection
    ) noexcept {
        return canonical_connection(connection).origin;
    }

    ZoneId destination_of(
        const SearchConnection& connection
    ) noexcept {
        return canonical_connection(connection).destination;
    }

    ConnectionMetrics metrics_of(
        const SearchConnection& connection
    ) {
        return *compute_connection_metrics(canonical_connection(connection));
    }

    Time departure_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).departure_time;
    }

    Time arrival_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).arrival_time;
    }

    Time journey_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).journey_time;
    }

    Time transfer_time_of(
        const SearchConnection& connection
    ) {
        const auto metrics = metrics_of(connection);
        return metrics.transfer_wait_time + metrics.transfer_walk_time;
    }

    TransferCount transfer_count_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).transfer_count;
    }

    double fare_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).fare;
    }

    std::vector<ConnectionSegmentId> connection_segment_trace(
        const SearchConnection& connection
    ) {
        return connection_segments_of(canonical_connection(connection).trace);
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
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
                "  tasks = {:>8}  max_transfers = {}"
                , network.route_segments     .size()
                , network.connection_segments.size()
                , tasks.size()
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
                "search setup: tasks = {:>8}  fare_scale = {:.6f}  time_domain = task"
                , tasks.size()
                , search_cost.fare_scale
            )
            , LogLevel::Info
        );
        log(
            format_search_pruning_execution_summary(summarize(effective_pruning_execution))
            , LogLevel::Info
        );

        if (tasks.empty()) {
            status("search: no positive-demand search tasks available");
        }

        const auto batches = build_search_batches(tasks);
        const auto residual_reverse_graph = build_residual_reverse_graph(
              network.route_segments
            , network.connection_segments
        );
        log(
            fmt::format(
                "search batching: batches = {:>8}  tasks = {:>8}"
                , batches.size()
                , tasks.size()
            )
            , LogLevel::Info
        );

        for (std::size_t i = 0; i < batches.size(); ++i) {
            const auto& batch = batches[i];
            const auto total_found = search_connection_count(result);
            if (i == 0 || (i % kTaskProgressStep) == 0 || (i + 1) == batches.size()) {
                status(
                    fmt::format(
                          "search: batch {}/{} origin={} interval={} tasks={} capacity_iteration={} total_found={}"
                        , i + 1
                        , batches.size()
                        , batch.key.origin.get()
                        , batch.key.interval.get()
                        , batch.tasks.size()
                        , diagnostics.capacity_iteration
                        , total_found
                    )
                );
            }
            log(
                fmt::format(
                      "search batch start: {}/{} origin={} interval={} tasks={} capacity_iteration={} cumulative_found={}"
                    , i + 1
                    , batches.size()
                    , batch.key.origin.get()
                    , batch.key.interval.get()
                    , batch.tasks.size()
                    , diagnostics.capacity_iteration
                    , total_found
                )
                , LogLevel::Info
            );
            MATHFP_TRY(
                search_batch_connections(
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
                    , diagnostics
                    , i
                    , batches.size()
                    , result.task_results
                )
            );
        }

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

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , double                     fare_scale
        , const SearchParams&        params
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        MATHFP_TRY_LET(
              SearchCostContext
            , search_cost
            , make_base_search_cost_context(params.impedance, fare_scale)
        );
        return search_connections_branch_and_bound(
              network
            , tasks
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , double                     fare_scale
        , const SearchParams&        params
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , fare_scale
            , params
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

}  // namespace timetable::domain::assignment
