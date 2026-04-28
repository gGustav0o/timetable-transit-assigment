#include "timetable/domain/assignment/search/search.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
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
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/assignment/search/branch_state.hpp"
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
         * @brief Parameter-independent metrics of a partial connection prefix.
         *
         * Search may use parameterized evaluations of these metrics for pruning,
         * but the branch state keeps the metrics themselves separate from those
         * evaluations. The invariant is that these values are the incremental
         * fold of SearchPartialTrace from the root to this branch.
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
        };

        struct SearchBranch final {
            SearchPartialTrace   trace{};
            SearchPartialMetrics metrics{};
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
            std::size_t rejected_dominance_or_tolerance{};
            std::size_t completed_connections{};
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

        struct BatchReachability final {
            std::map<ZoneId, std::unordered_set<EndpointKey>> nodes_by_destination{};
        };

        struct PhysicalReverseGraph final {
            std::unordered_map<EndpointKey, std::vector<EndpointKey>> predecessors_by_node{};
        };

        constexpr std::size_t kTaskProgressStep    = 10;
        constexpr std::size_t kSearchHeartbeatStep = 100'000;
        constexpr std::size_t kInitialTaskBranchReserve = 4'096;

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

        [[nodiscard]] PhysicalReverseGraph build_physical_reverse_graph(
            std::span<const RouteSegment> route_segments
        ) {
            PhysicalReverseGraph graph;
            for (const auto& segment : route_segments) {
                const auto from = physical_from_key(segment);
                const auto to   = physical_to_key(segment);
                graph.predecessors_by_node[to].push_back(from);
                graph.predecessors_by_node.try_emplace(from);
            }
            return graph;
        }

        [[nodiscard]] std::unordered_set<EndpointKey> reverse_reachable_nodes(
              const PhysicalReverseGraph& reverse_graph
            , EndpointKey                 destination
        ) {
            std::unordered_set<EndpointKey> reachable;
            std::vector<EndpointKey> frontier;
            reachable.insert(destination);
            frontier.push_back(destination);

            while (!frontier.empty()) {
                const auto current = frontier.back();
                frontier.pop_back();

                const auto it = reverse_graph.predecessors_by_node.find(current);
                if (it == reverse_graph.predecessors_by_node.end()) {
                    continue;
                }
                for (const auto predecessor : it->second) {
                    if (reachable.insert(predecessor).second) {
                        frontier.push_back(predecessor);
                    }
                }
            }

            return reachable;
        }

        [[nodiscard]] BatchReachability build_batch_reachability(
              const PhysicalReverseGraph&   reverse_graph
            , std::span<const BatchTaskRef> tasks
        ) {
            BatchReachability reachability;

            for (const auto& task_ref : tasks) {
                const auto destination = task_ref.task->destination;
                if (reachability.nodes_by_destination.contains(destination)) {
                    continue;
                }
                reachability.nodes_by_destination.emplace(
                      destination
                    , reverse_reachable_nodes(reverse_graph, endpoint_key(destination))
                );
            }

            return reachability;
        }

        [[nodiscard]] bool can_reach_destination_topologically(
              const BatchReachability& reachability
            , EndpointKey              from
            , ZoneId                   destination
        ) noexcept {
            const auto it = reachability.nodes_by_destination.find(destination);
            if (it == reachability.nodes_by_destination.end()) {
                return true;
            }
            return it->second.contains(from);
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

        SearchNodeKey search_node_key(
            const SearchBranch& branch
        ) noexcept {
            return SearchNodeKey{
                  .physical   = branch.trace.current_physical
                , .occurrence = branch.trace.current_occurrence
                , .transfer   = search_transfer_context(branch)
            };
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

        PartialPruningMetrics make_partial_pruning_metrics(
              const SearchBranch&    branch
            , const SearchImpedance& impedance
            , double                 fare_scale
        ) {
            const auto journey_time = partial_journey_time(branch.metrics);

            return PartialPruningMetrics{
                  .departure    = *branch.metrics.departure
                , .arrival      = *branch.metrics.current_time
                , .journey_time = journey_time
                , .walk_time    = partial_walk_time(branch.metrics)
                , .transfers    = branch.metrics.transfers
                , .fare         = branch.metrics.fare
                , .impedance    = connection_impedance_value(
                      partial_impedance_components(branch.metrics)
                    , impedance
                    , fare_scale
                )
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

        [[nodiscard]] bool complete_connection_dominates_partial_prefix(
              const CompleteConnectionMetrics& complete
            , const PartialPruningMetrics&     partial
        ) noexcept {
            const auto no_worse =
                   complete.departure.value() >= partial.departure.value()
                && complete.arrival  .value() <= partial.arrival  .value()
                && complete.impedance          <= partial.impedance
                && complete.transfers.get()    <= partial.transfers.get();

            const auto strictly_better =
                   complete.departure.value() > partial.departure.value()
                || complete.arrival  .value() < partial.arrival  .value()
                || complete.impedance          < partial.impedance
                || complete.transfers.get()    < partial.transfers.get();

            return no_worse && strictly_better;
        }

        [[nodiscard]] bool partial_prefix_violates_complete_tolerances(
              const PartialPruningMetrics&           partial
            , const CompleteConnectionMetricSummary& summary
            , const ChoiceTolerances&                tolerances
        ) noexcept {
            return !within_complete_connection_tolerances(
                  CompleteConnectionMetrics{
                      .departure   = partial.departure
                    , .arrival     = partial.arrival
                    , .journey_time = partial.journey_time
                    , .transfers    = partial.transfers
                    , .impedance    = partial.impedance
                  }
                , summary
                , tolerances
            );
        }

        [[nodiscard]] bool violates_known_complete_bounds(
              const SearchBranch&                branch
            , const CompleteConnectionRetention& complete_retention
            , const SearchParams&                params
            , const ChoiceConfig&                choice_config
            , double                             fare_scale
        ) noexcept {
            if (complete_retention.alternatives.empty()
                || !branch.metrics.departure.has_value()
                || !branch.metrics.current_time.has_value()) {
                return false;
            }

            const auto partial = make_partial_pruning_metrics(
                  branch
                , params.impedance
                , fare_scale
            );
            for (const auto& complete : complete_retention.alternatives) {
                if (complete_connection_dominates_partial_prefix(
                      complete.metrics
                    , partial
                )) {
                    return true;
                }
            }

            if (choice_config.rollout_stage != ChoiceRolloutStage::ExactAndApproximate) {
                return false;
            }
            return partial_prefix_violates_complete_tolerances(
                  partial
                , summarize_complete_metrics(complete_retention)
                , params.choice_tolerances
            );
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

        [[nodiscard]] SearchPartialMetrics extend_metrics_with_timed(
              SearchPartialMetrics      metrics
            , const ConnectionSegment& segment
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
            return metrics;
        }

        [[nodiscard]] SearchBranch extend_with_timed(
              const SearchBranch&      branch
            , std::size_t              branch_index
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
        ) {
            return SearchBranch{
                  .trace   = extend_trace_with_timed(
                        branch.trace
                      , branch_index
                      , branch.metrics
                      , segment
                      , route_segment
                  )
                , .metrics = extend_metrics_with_timed(branch.metrics, segment)
            };
        }

        std::optional<SearchBranch> extend_branch(
              const BranchArena&         branches
            , std::size_t                branch_index
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
        ) {
            const auto& route_segment = route_segment_at(network, successor.route_segment);
            if (is_walk_connection(successor)) {
                const auto next_physical = physical_to_key(route_segment);
                if (branch_revisits_physical(branches, branch, next_physical)) {
                    return std::nullopt;
                }
                return extend_with_walk(branch, branch_index, route_segment, successor.id);
            }

            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            if (branch_revisits_occurrence(branches, branch, next_occurrence)) {
                return std::nullopt;
            }
            return extend_with_timed(branch, branch_index, successor, route_segment);
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

        SearchPruningDecision retain_branch(
              const SearchBranch&               branch
            , SearchTaskRetention&              retention
            , const SearchParams&               params
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
            , double                            fare_scale
        ) {
            if (!branch.metrics.departure.has_value() || !branch.metrics.current_time.has_value()) {
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                };
            }

            ++pruning_stats.evaluated_candidates;
            auto metrics = make_partial_pruning_metrics(branch, params.impedance, fare_scale);
            const auto node  = search_node_key(branch);
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

        [[nodiscard]] std::vector<std::size_t> reachable_batch_task_positions(
              std::span<const BatchTaskRef> tasks
            , const BatchReachability&      reachability
            , EndpointKey                   from
        ) {
            std::vector<std::size_t> active;
            active.reserve(tasks.size());
            for (std::size_t i = 0; i < tasks.size(); ++i) {
                if (can_reach_destination_topologically(
                      reachability
                    , from
                    , tasks[i].task->destination
                )) {
                    active.push_back(i);
                }
            }
            return active;
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
            , double                     fare_scale
            , SearchTaskRetention&       retention
            , TaskSearchStats&           stats
            , ZoneId                     destination
        ) {
            MATHFP_TRY_LET(
                  std::optional<SearchConnection>
                , complete
                , complete_connection(branch, network, params.transfers, destination)
            );
            if (complete.has_value()) {
                ++stats.completed_connections;
                const auto retention_decision = retain_exact_complete_connection(
                      retention.complete_connections
                    , std::move(*complete)
                    , params
                    , fare_scale
                );
                stats.removed_complete_dominated += retention_decision.removed_dominated;
                if (!retention_decision.accepted) {
                    ++stats.rejected_complete_dominance;
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> search_batch_connections(
              const SearchBatch&                 batch
            , const PreprocessedNetwork&        network
            , const PhysicalReverseGraph&        reverse_graph
            , double                            fare_scale
            , const SearchParams&               params
            , const ChoiceConfig&                choice_config
            , const SearchPruningExecutionPlan& pruning_execution
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
            const auto                        reachability = build_batch_reachability(
                  reverse_graph
                , batch_task_span
            );

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
                      }
                }
            );
            branch_active_tasks.push_back(
                reachable_batch_task_positions(
                      batch_task_span
                    , reachability
                    , endpoint_key(batch.key.origin)
                )
            );
            if (branch_active_tasks.back().size() != batch.tasks.size()) {
                stats.rejected_reachability += batch.tasks.size() - branch_active_tasks.back().size();
                std::vector<bool> is_active(batch.tasks.size(), false);
                for (const auto task_pos : branch_active_tasks.back()) {
                    is_active[task_pos] = true;
                }
                for (std::size_t task_pos = 0; task_pos < batch.tasks.size(); ++task_pos) {
                    if (!is_active[task_pos]) {
                        ++task_stats[task_pos].rejected_reachability;
                    }
                }
            }
            current_frontier.push_back(0);

            status(
                fmt::format(
                      "search: batch {}/{} origin={} interval={} tasks={} frontier={} found={}"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , batch.key.interval.get()
                    , batch.tasks.size()
                    , current_frontier.size()
                    , retained_connection_count(retentions)
                )
            );

            while (!current_frontier.empty() || !next_frontier.empty()) {
                if (current_frontier.empty()) {
                    current_frontier.swap(next_frontier);
                }

                stats.max_current_frontier = std::max(stats.max_current_frontier, current_frontier.size());
                stats.max_next_frontier    = std::max(stats.max_next_frontier   , next_frontier   .size());

                const auto branch_index = current_frontier.front();
                current_frontier.pop_front();
                const auto branch = branches[branch_index];
                const auto active_tasks = std::span<const std::size_t>{
                      branch_active_tasks[branch_index].data()
                    , branch_active_tasks[branch_index].size()
                };
                ++stats.expanded_branches;

                if ((stats.expanded_branches % kSearchHeartbeatStep) == 0) {
                    status(
                        fmt::format(
                              "search: batch {}/{} origin={} interval={} tasks={} expanded={} accepted={} found={} frontier={}/{}"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , batch.key.interval.get()
                            , batch.tasks.size()
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_connection_count(retentions)
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                    );
                    log(
                        fmt::format(
                              "search heartbeat: batch={:>8} origin={:>4} interval={:>4} tasks={:>5} expanded={:>8} generated={:>8}"
                              " accepted={:>8} found={:>8} rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                              " pruning(exact/approx/inserted/skipped)={}/{}/{}/{}"
                              " frontier={}/{}"
                            , static_cast<std::int64_t>(batch_index)
                            , batch.key.origin.get()
                            , batch.key.interval.get()
                            , batch.tasks.size()
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

                    auto candidate = extend_branch(branches, branch_index, branch, network, successor);
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
                                , fare_scale
                                , retentions[task_pos]
                                , task_stats[task_pos]
                                , batch.tasks[task_pos].task->destination
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

                    std::vector<std::size_t> next_active_tasks;
                    next_active_tasks.reserve(active_tasks.size());
                    for (const auto task_pos : active_tasks) {
                        if (!can_reach_destination_topologically(
                              reachability
                            , candidate->trace.current_physical
                            , batch.tasks[task_pos].task->destination
                        )) {
                            ++task_stats[task_pos].rejected_reachability;
                            ++stats.rejected_reachability;
                            continue;
                        }

                        if (violates_known_complete_bounds(
                              *candidate
                            , retentions[task_pos].complete_connections
                            , params
                            , choice_config
                            , fare_scale
                        )) {
                            ++task_stats[task_pos].rejected_dominance_or_tolerance;
                            continue;
                        }

                        const auto pruning_decision = retain_branch(
                              *candidate
                            , retentions[task_pos]
                            , params
                            , pruning_execution
                            , stats.pruning
                            , fare_scale
                        );
                        if (!pruning_decision.accepted) {
                            ++task_stats[task_pos].rejected_dominance_or_tolerance;
                            continue;
                        }
                        next_active_tasks.push_back(task_pos);
                    }
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
                stats.rejected_complete_dominance += task_stats[task_pos].rejected_complete_dominance;
                stats.removed_complete_dominated  += task_stats[task_pos].removed_complete_dominated;
                stats.rejected_complete_tolerance += task_stats[task_pos].rejected_complete_tolerance;
                batch_final_found += task_result.connections.size();
                batch_retained_before_tolerance += before_tolerance;

                log(
                    fmt::format(
                          "search batch task done: batch={}/{} task={} origin={} destination={} interval={} found={:>8}"
                          " retained_complete={:>8} complete_rejected(dominance/tolerance)={}/{} complete_removed_dominated={} reachability_pruned={}"
                        , batch_index + 1
                        , batch_count
                        , task_result.task.index.get()
                        , task_result.task.origin.get()
                        , task_result.task.destination.get()
                        , task_result.task.interval.id.get()
                        , task_result.connections.size()
                        , before_tolerance
                        , task_stats[task_pos].rejected_complete_dominance
                        , task_stats[task_pos].rejected_complete_tolerance
                        , task_stats[task_pos].removed_complete_dominated
                        , task_stats[task_pos].rejected_reachability
                    )
                    , LogLevel::Info
                );
            }

            log(
                fmt::format(
                      "search batch done: {}/{} origin={} interval={} tasks={} found={:>8} completed={:>8}"
                      " retained_complete={:>8} complete_rejected(dominance/tolerance)={}/{} complete_removed_dominated={}"
                      " expanded={:>8} generated={:>8} accepted={:>8}"
                      " rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{} max_frontier={}/{}"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , batch.key.interval.get()
                    , batch.tasks.size()
                    , batch_final_found
                    , stats.completed_connections
                    , batch_retained_before_tolerance
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
                    , stats.max_current_frontier
                    , stats.max_next_frontier
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
          const InputModel&              input
        , const SearchTimePaddingPolicy& padding_policy
        , const SplitParams&             split
    ) {
        MATHFP_TRY_LET(
              SearchTimePadding
            , padding
            , resolve_search_time_padding(padding_policy, split)
        );

        auto intervals_result = interval_lookup(input);
        if (!intervals_result) {
            return mathfp::unexpected(std::move(intervals_result.error()));
        }
        auto intervals = std::move(*intervals_result);
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
                    expand_interval_to_search_window(*interval_it->second, padding)
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
        , double                     fare_scale
        , const SearchParams&        params
        , const ChoiceConfig&         choice_config
        , const SearchPruningExecutionPlan* pruning_execution
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

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
                  SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
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
                , fare_scale
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
        const auto physical_reverse_graph = build_physical_reverse_graph(network.route_segments);
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
                          "search: batch {}/{} origin={} interval={} tasks={} total_found={}"
                        , i + 1
                        , batches.size()
                        , batch.key.origin.get()
                        , batch.key.interval.get()
                        , batch.tasks.size()
                        , total_found
                    )
                );
            }
            log(
                fmt::format(
                      "search batch start: {}/{} origin={} interval={} tasks={} cumulative_found={}"
                    , i + 1
                    , batches.size()
                    , batch.key.origin.get()
                    , batch.key.interval.get()
                    , batch.tasks.size()
                    , total_found
                )
                , LogLevel::Info
            );
            MATHFP_TRY(
                search_batch_connections(
                      batch
                    , network
                    , physical_reverse_graph
                    , fare_scale
                    , params
                    , choice_config
                    , effective_pruning_execution
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

}  // namespace timetable::domain::assignment
