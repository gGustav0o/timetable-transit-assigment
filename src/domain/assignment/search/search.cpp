#include "timetable/domain/assignment/search/search.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iterator>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
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

        bool is_terminal_zone(
              const SearchBranch& branch
            , ZoneId              task_destination
        ) noexcept {
            return branch.metrics.departure.has_value()
                && branch.trace.current_physical.kind == EndpointKind::Zone
                && branch.trace.current_physical.id   != branch.trace.origin.get()
                && branch.trace.current_physical.id   != task_destination.get();
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
        void for_each_timed_successor(
              const PreprocessedNetwork& network
            , EndpointKey                physical_from
            , std::optional<Time>        current_time
            , const TransferLimits&      limits
            , Visitor&&                  visit
        ) {
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
                for (std::size_t i = start; i < end; ++i) {
                    visit(network.connection_index.boarding_order[i]);
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
                visit(network.connection_index.boarding_order[idx]);
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
            , const SearchBranch&        branch
            , const TransferLimits&      limits
            , Visitor&&                  visit
        ) {
            auto&& visitor = visit;
            const auto lookup = preprocessing::lookup_from(
                  network.route_index
                , network.connection_index
                , branch.trace.current_physical
            );

            for (const auto connection_id : lookup.walk_connections) {
                visitor(connection_id);
            }

            for_each_timed_successor(
                  network
                , branch.trace.current_physical
                , branch.metrics.current_time
                , limits
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

        mathfp::Expected<SearchTaskResult> search_task_connections(
              const SearchTask&                 task
            , const PreprocessedNetwork&        network
            , double                            fare_scale
            , const SearchParams&               params
            , const ChoiceConfig&                choice_config
            , const SearchPruningExecutionPlan& pruning_execution
            , std::size_t                       task_index
            , std::size_t                       task_count
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            SearchTaskResult result{
                  .task        = task
                , .connections = {}
            };
            SearchTaskRetention           retention{ .task = task.index };
            TaskSearchStats                   stats;
            BranchArena                       branches;
            const SearchTimeDomain*           first_departure_domain = &task.departure_domain;

            branches.reserve(kInitialTaskBranchReserve);

            std::deque<std::size_t> current_frontier;
            std::deque<std::size_t> next_frontier;
            branches.push_back(
                SearchBranch{
                      .trace = SearchPartialTrace{
                            .origin                   = task.origin
                          , .current_physical         = endpoint_key(task.origin)
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
            current_frontier.push_back(0);

            status(
                fmt::format(
                      "search: task {}/{} origin={} destination={} interval={} frontier={} found={}"
                    , task_index + 1
                    , task_count
                    , task.origin.get()
                    , task.destination.get()
                    , task.interval.id.get()
                    , current_frontier.size()
                    , retention.complete_connections.alternatives.size()
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
                ++stats.expanded_branches;

                if ((stats.expanded_branches % kSearchHeartbeatStep) == 0) {
                    status(
                        fmt::format(
                              "search: task {}/{} origin={} destination={} interval={} expanded={} accepted={} found={} frontier={}/{}"
                            , task_index + 1
                            , task_count
                            , task.origin.get()
                            , task.destination.get()
                            , task.interval.id.get()
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retention.complete_connections.alternatives.size()
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                    );
                    log(
                        fmt::format(
                              "search heartbeat: task={:>8} origin={:>4} destination={:>4} interval={:>4} expanded={:>8} generated={:>8}"
                              " accepted={:>8} found={:>8} rejected(time_domain/feasibility/reboarding/cycles/limit/dominance)={}/{}/{}/{}/{}/{}"
                              " pruning(exact/approx/inserted/skipped)={}/{}/{}/{}"
                              " frontier={}/{}"
                            , task.index.get()
                            , task.origin.get()
                            , task.destination.get()
                            , task.interval.id.get()
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , retention.complete_connections.alternatives.size()
                            , stats.rejected_time_domain
                            , stats.rejected_feasibility
                            , stats.rejected_reboarding
                            , stats.rejected_cycles
                            , stats.rejected_transfer_limit
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

                if (is_terminal_zone(branch, task.destination)) {
                    continue;
                }

                if (is_complete_connection(branch, task.destination)) {
                    MATHFP_TRY_LET(
                          std::optional<SearchConnection>
                        , complete
                        , complete_connection(branch, network, params.transfers, task.destination)
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
                    continue;
                }

                for_each_successor(network, branch, params.transfers, [&](ConnectionSegmentId successor_id) {
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

                    const auto pruning_decision = retain_branch(
                          *candidate
                        , retention
                        , params
                        , pruning_execution
                        , stats.pruning
                        , fare_scale
                    );
                    if (!pruning_decision.accepted) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }
                    ++stats.accepted_branches;
                    const auto same_level =
                        is_walk_connection(successor) || !branch.metrics.departure.has_value();
                    branches.push_back(std::move(*candidate));
                    const auto candidate_index = branches.size() - 1;
                    if (same_level) {
                        current_frontier.push_back(candidate_index);
                    } else {
                        next_frontier   .push_back(candidate_index);
                    }
                });
            }

            const auto before_tolerance = retention.complete_connections.alternatives.size();
            result.connections = finalize_complete_connection_retention(
                  retention.complete_connections
                , params.choice_tolerances
                , choice_config.rollout_stage
            );
            stats.rejected_complete_tolerance = before_tolerance - result.connections.size();

            log(
                fmt::format(
                      "search task done: {}/{} task={} origin={} destination={} interval={} found={:>8} completed={:>8}"
                      " retained_complete={:>8} complete_rejected(dominance/tolerance)={}/{} complete_removed_dominated={}"
                      " expanded={:>8} generated={:>8} accepted={:>8}"
                      " rejected(time_domain/feasibility/reboarding/cycles/limit/dominance)={}/{}/{}/{}/{}/{} max_frontier={}/{}"
                    , task_index + 1
                    , task_count
                    , task.index.get()
                    , task.origin.get()
                    , task.destination.get()
                    , task.interval.id.get()
                    , result.connections.size()
                    , stats.completed_connections
                    , before_tolerance
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

            return result;
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

        MATHFP_TRY_LET(
              std::map<IntervalId, const TimeInterval*>
            , intervals
            , interval_lookup(input)
        );
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

        for (std::size_t i = 0; i < tasks.size(); ++i) {
            const auto& task = tasks[i];
            const auto total_found = search_connection_count(result);
            if (i == 0 || (i % kTaskProgressStep) == 0 || (i + 1) == tasks.size()) {
                status(
                    fmt::format(
                          "search: task {}/{} origin={} destination={} interval={} total_found={}"
                        , i + 1
                        , tasks.size()
                        , task.origin.get()
                        , task.destination.get()
                        , task.interval.id.get()
                        , total_found
                    )
                );
            }
            log(
                fmt::format(
                      "search task start: {}/{} task={} origin={} destination={} interval={} cumulative_found={}"
                    , i + 1
                    , tasks.size()
                    , task.index.get()
                    , task.origin.get()
                    , task.destination.get()
                    , task.interval.id.get()
                    , total_found
                )
                , LogLevel::Info
            );
            MATHFP_TRY_LET(
                  SearchTaskResult
                , task_result
                , search_task_connections(
                      task
                    , network
                    , fare_scale
                    , params
                    , choice_config
                    , effective_pruning_execution
                    , i
                    , tasks.size()
                )
            );
            result.task_results.push_back(std::move(task_result));
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
