#include "timetable/domain/assignment/steps.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/numeric.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        struct PartialConnectionLabel final {
            Time departure{};
            Time arrival{};
            Time journey_time{};
            // Sum of walk-segment durations accumulated along the partial connection.
            Time walk_time{};
            TransferCount transfers{};
            double fare{};
            double impedance{};
        };

        struct NodeLocalSearchSummary final {
            double min_impedance{ std::numeric_limits<double>::infinity() };
            double min_journey_time{ std::numeric_limits<double>::infinity() };
            // Paper-consistent node-local summary values retained alongside the
            // current relevance/tolerance checks.
            double min_walk_time{ std::numeric_limits<double>::infinity() };
            double min_transfers{ std::numeric_limits<double>::infinity() };
            double min_fare{ std::numeric_limits<double>::infinity() };
        };

        struct NodeConnectionSet final {
            std::vector<PartialConnectionLabel> labels{};
            NodeLocalSearchSummary summary{};
        };

        struct SearchNodeKey final {
            EndpointKey                    physical{};
            std::optional<StopOccurrenceKey> occurrence{};

            auto operator<=>(const SearchNodeKey&) const = default;
        };

        struct SearchNodeKeyHash final {
            std::size_t operator()(const SearchNodeKey& key) const noexcept {
                std::size_t seed = 17u;
                seed = seed * 31u + std::hash<std::int64_t>{}(static_cast<std::int64_t>(key.physical.kind));
                seed = seed * 31u + std::hash<std::int64_t>{}(key.physical.id);
                seed = seed * 31u + std::hash<bool>{}(key.occurrence.has_value());
                if (key.occurrence.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->stop.get());
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->position.get());
                }
                return seed;
            }
        };

        struct SearchBranch final {
            ZoneId origin{};
            EndpointKey current_physical{};
            std::optional<StopOccurrenceKey> current_occurrence{};
            std::optional<Time> departure{};
            std::optional<Time> current_time{};
            // Total accumulated walk duration over all walk segments seen so far.
            Time walk_time{};
            Time access_walk_time{};
            Time transfer_time{};
            TransferCount transfers{};
            double fare{};
            std::vector<ConnectionSegmentId> segments{};
            std::vector<EndpointKey> visited_physical{};
            std::vector<StopOccurrenceKey> visited_occurrences{};
            const ConnectionSegment* last_timed_segment{};
            const RouteSegment* last_timed_route_segment{};
        };

        using NodeConnectionMap = std::unordered_map<SearchNodeKey, NodeConnectionSet, SearchNodeKeyHash>;

        const RouteSegment& route_segment_at(
            const PreprocessedNetwork& network
            , RouteSegmentId id
        ) {
            return network.route_segments.at(static_cast<std::size_t>(id.get()));
        }

        const ConnectionSegment& connection_segment_at(
            const PreprocessedNetwork& network
            , ConnectionSegmentId id
        ) {
            return network.connection_segments.at(static_cast<std::size_t>(id.get()));
        }

        double normalized_fare(
            double fare
            , double fare_scale
        ) noexcept {
            return fare_scale > 0.0 ? fare / fare_scale : 0.0;
        }

        double branch_impedance_value(
            Time journey_time
            , TransferCount transfers
            , double fare
            , const SearchImpedance& impedance
            , double fare_scale
        ) noexcept {
            return linear_connection_impedance(
                mathfp::units::as_dimless(impedance.a_journey_time)
                , mathfp::units::as_dimless(impedance.a_transfers)
                , mathfp::units::as_dimless(impedance.a_fare)
                , journey_time.value()
                , static_cast<double>(transfers.get())
                , normalized_fare(fare, fare_scale)
            );
        }

        std::vector<ZoneId> search_origins(
            const PreprocessedNetwork& network
        ) {
            std::unordered_set<std::int64_t> ids;
            for (const auto& key : network.connection_index.walk_buckets) {
                if (key.kind == EndpointKind::Zone) {
                    ids.insert(key.id);
                }
            }

            std::vector<ZoneId> origins;
            origins.reserve(ids.size());
            for (const auto id : ids) {
                origins.push_back(ZoneId{ id });
            }
            std::sort(origins.begin(), origins.end(), [](ZoneId a, ZoneId b) {
                return a.get() < b.get();
            });
            return origins;
        }

        SearchNodeKey search_node_key(
            const SearchBranch& branch
        ) noexcept {
            return SearchNodeKey{
                .physical = branch.current_physical,
                .occurrence = branch.current_occurrence
            };
        }

        bool branch_revisits_physical(
            const SearchBranch& branch
            , EndpointKey next
        ) {
            return std::find(branch.visited_physical.begin(), branch.visited_physical.end(), next)
                != branch.visited_physical.end();
        }

        bool branch_revisits_occurrence(
            const SearchBranch& branch
            , StopOccurrenceKey next
        ) {
            return std::find(branch.visited_occurrences.begin(), branch.visited_occurrences.end(), next)
                != branch.visited_occurrences.end();
        }

        bool is_complete_connection(
            const SearchBranch& branch
        ) noexcept {
            return branch.departure.has_value()
                && branch.current_physical.kind == EndpointKind::Zone
                && branch.current_physical.id != branch.origin.get();
        }

        PartialConnectionLabel make_partial_label(
            const SearchBranch& branch
            , const SearchImpedance& impedance
            , double fare_scale
        ) {
            const auto journey_time = Time{
                branch.current_time->value() - branch.departure->value()
            };

            return PartialConnectionLabel{
                .departure = *branch.departure
                , .arrival = *branch.current_time
                , .journey_time = journey_time
                , .walk_time = branch.walk_time
                , .transfers = branch.transfers
                , .fare = branch.fare
                , .impedance = branch_impedance_value(
                    journey_time
                    , branch.transfers
                    , branch.fare
                    , impedance
                    , fare_scale
                )
            };
        }

        bool is_relevant(
            const PartialConnectionLabel& candidate
            , const NodeConnectionSet& known
        ) noexcept {
            for (const auto& existing : known.labels) {
                if (existing.departure.value() >= candidate.departure.value()
                    && existing.arrival.value() <= candidate.arrival.value()
                    && existing.impedance <= candidate.impedance
                    && existing.transfers.get() <= candidate.transfers.get()) {
                    return false;
                }
            }
            return true;
        }

        bool within_search_tolerances(
            const PartialConnectionLabel& candidate
            , const NodeLocalSearchSummary& summary
            , const SearchTolerances& tolerances
            , const TransferLimits& limits
        ) noexcept {
            return candidate.transfers <= limits.max_transfers
                && candidate.impedance <= mathfp::units::as_dimless(tolerances.imp_mult) * summary.min_impedance
                    + mathfp::units::as_dimless(tolerances.imp_add)
                && candidate.journey_time.value()
                    <= mathfp::units::as_dimless(tolerances.jt_mult) * summary.min_journey_time
                        + mathfp::units::as_dimless(tolerances.jt_add)
                && static_cast<double>(candidate.transfers.get())
                    <= mathfp::units::as_dimless(tolerances.nt_mult) * summary.min_transfers
                        + mathfp::units::as_dimless(tolerances.nt_add);
        }

        void update_node_local_search_summary(
            NodeLocalSearchSummary& summary
            , const PartialConnectionLabel& label
        ) noexcept {
            summary.min_impedance = std::min(summary.min_impedance, label.impedance);
            summary.min_journey_time = std::min(summary.min_journey_time, label.journey_time.value());
            summary.min_walk_time = std::min(summary.min_walk_time, label.walk_time.value());
            summary.min_transfers = std::min(
                summary.min_transfers
                , static_cast<double>(label.transfers.get())
            );
            summary.min_fare = std::min(summary.min_fare, label.fare);
        }

        void insert_label(
            NodeConnectionMap& known_connections
            , SearchNodeKey node
            , PartialConnectionLabel label
        ) {
            auto& known = known_connections[node];
            const auto it = std::lower_bound(
                known.labels.begin()
                , known.labels.end()
                , label.arrival.value()
                , [](const PartialConnectionLabel& lhs, double arrival_value) {
                    return lhs.arrival.value() < arrival_value;
                }
            );
            known.labels.insert(it, label);
            update_node_local_search_summary(known.summary, label);
        }

        std::optional<std::pair<std::size_t, std::size_t>> timed_bucket_range(
            const preprocessing::ConnectionSegmentIndex& index
            , StopOccurrenceKey from
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

        std::vector<ConnectionSegmentId> timed_successors(
            const PreprocessedNetwork& network
            , EndpointKey physical_from
            , std::optional<Time> current_time
            , const TransferLimits& limits
        ) {
            std::vector<ConnectionSegmentId> out;
            if (physical_from.kind != EndpointKind::Stop) {
                return out;
            }

            const auto bucket = preprocessing::find_bucket(
                network.connection_index.boarding_stop_buckets
                , StopId{ physical_from.id }
            );
            if (!bucket) {
                return out;
            }

            const auto bucket_index = *bucket;
            const auto start = network.connection_index.boarding_offsets[bucket_index];
            const auto end = network.connection_index.boarding_offsets[bucket_index + 1];
            if (start >= end) {
                return out;
            }

            if (!current_time.has_value()) {
                out.insert(
                    out.end()
                    , network.connection_index.boarding_order.begin() + static_cast<std::ptrdiff_t>(start)
                    , network.connection_index.boarding_order.begin() + static_cast<std::ptrdiff_t>(end)
                );
                return out;
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

            for (auto it = begin; it != departures_end; ++it) {
                if (it->value() > latest.value()) {
                    break;
                }

                const auto idx = static_cast<std::size_t>(
                    std::distance(network.connection_index.boarding_departures.begin(), it)
                );
                out.push_back(network.connection_index.boarding_order[idx]);
            }

            return out;
        }

        bool is_same_line_transfer_candidate(
            const SearchBranch& branch
            , const ConnectionSegment& successor
            , const RouteSegment& successor_route_segment
        ) noexcept {
            if (!branch.last_timed_segment || !branch.last_timed_route_segment) {
                return false;
            }
            if (!is_timed_connection(successor)) {
                return false;
            }
            if (!same_line(*branch.last_timed_route_segment, successor_route_segment)) {
                return false;
            }
            return successor.trip != branch.last_timed_segment->trip;
        }

        bool is_repeated_stop_reboarding_case(
            const SearchBranch& branch
            , const ConnectionSegment& successor
            , const RouteSegment& successor_route_segment
        ) noexcept {
            if (!is_same_line_transfer_candidate(branch, successor, successor_route_segment)) {
                return false;
            }
            const auto* current_line = line_topology_of(*branch.last_timed_route_segment);
            const auto* successor_line = line_topology_of(successor_route_segment);
            if (!current_line || !successor_line) {
                return false;
            }
            if (current_line->to.stop != successor_line->from.stop) {
                return false;
            }
            if (!branch.last_timed_segment->to_index.has_value() || !successor.from_index.has_value()) {
                return false;
            }
            return successor.from_index.value() < branch.last_timed_segment->to_index.value();
        }

        std::optional<Time> same_trip_continuation_arrival(
            const SearchBranch& branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment& successor
            , const RouteSegment& successor_route_segment
        ) noexcept {
            if (!branch.last_timed_segment || !branch.last_timed_route_segment) {
                return std::nullopt;
            }
            if (!branch.last_timed_segment->trip.has_value()
                || !branch.last_timed_segment->to_index.has_value()
                || !successor.to_index.has_value()) {
                return std::nullopt;
            }

            const auto current_stop = occurrence_key(line_topology_of(*branch.last_timed_route_segment)->to);
            const auto desired_route_to_index = successor.to_index.value();
            const auto range = timed_bucket_range(network.connection_index, current_stop);
            if (!range) {
                return std::nullopt;
            }

            const auto [start, end] = *range;
            for (std::size_t i = start; i < end; ++i) {
                const auto connection_id = network.connection_index.timed_order[i];
                const auto& continuation = connection_segment_at(network, connection_id);
                const auto& continuation_route_segment = route_segment_at(
                    network
                    , continuation.route_segment
                );
                if (!same_line(*branch.last_timed_route_segment, continuation_route_segment)) {
                    continue;
                }
                if (continuation.trip != branch.last_timed_segment->trip) {
                    continue;
                }
                if (continuation.from_index != branch.last_timed_segment->to_index) {
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
            const SearchBranch& branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment& successor
            , const RouteSegment& successor_route_segment
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

        SearchBranch extend_with_walk(
            SearchBranch branch
            , const RouteSegment& route_segment
            , ConnectionSegmentId segment_id
        ) {
            branch.current_physical = physical_to_key(route_segment);
            branch.current_occurrence = std::nullopt;
            branch.walk_time = Time{
                branch.walk_time.value() + route_segment.run_time.value()
            };
            branch.segments.push_back(segment_id);
            branch.visited_physical.push_back(branch.current_physical);

            if (branch.departure.has_value()) {
                branch.current_time = Time{
                    branch.current_time->value() + route_segment.run_time.value()
                };
                if (branch.current_physical.kind == EndpointKind::Stop) {
                    branch.transfer_time = Time{
                        branch.transfer_time.value() + route_segment.run_time.value()
                    };
                }
            } else {
                branch.access_walk_time = Time{
                    branch.access_walk_time.value() + route_segment.run_time.value()
                };
            }

            return branch;
        }

        SearchBranch extend_with_timed(
            SearchBranch branch
            , const ConnectionSegment& segment
            , const RouteSegment& route_segment
        ) {
            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            const auto had_departure = branch.departure.has_value();
            if (!had_departure) {
                branch.departure = Time{
                    segment.departure->value() - branch.access_walk_time.value()
                };
            } else {
                branch.transfer_time = Time{
                    branch.transfer_time.value()
                    + (segment.departure->value() - branch.current_time->value())
                };
                branch.transfers = TransferCount{ branch.transfers.get() + 1 };
            }

            branch.current_time = *segment.arrival;
            branch.current_physical = physical_to_key(route_segment);
            branch.current_occurrence = next_occurrence;
            branch.fare += segment.fare.value_or(0.0);
            branch.segments.push_back(segment.id);
            branch.visited_occurrences.push_back(next_occurrence);
            branch.last_timed_segment = &segment;
            branch.last_timed_route_segment = &route_segment;
            return branch;
        }

        std::optional<SearchBranch> extend_branch(
            SearchBranch branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment& successor
        ) {
            const auto& route_segment = route_segment_at(network, successor.route_segment);
            if (is_walk_connection(successor)) {
                const auto next_physical = physical_to_key(route_segment);
                if (branch_revisits_physical(branch, next_physical)) {
                    return std::nullopt;
                }
                return extend_with_walk(std::move(branch), route_segment, successor.id);
            }

            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            if (branch_revisits_occurrence(branch, next_occurrence)) {
                return std::nullopt;
            }
            return extend_with_timed(std::move(branch), successor, route_segment);
        }

        std::vector<ConnectionSegmentId> successor_ids(
            const PreprocessedNetwork& network
            , const SearchBranch& branch
            , const TransferLimits& limits
        ) {
            auto lookup = preprocessing::lookup_from(
                network.route_index
                , network.connection_index
                , branch.current_physical
            );

            std::vector<ConnectionSegmentId> out;
            out.reserve(lookup.walk_connections.size() + lookup.timed_connections.size());
            out.insert(out.end(), lookup.walk_connections.begin(), lookup.walk_connections.end());

            auto timed = timed_successors(
                network
                , branch.current_physical
                , branch.current_time
                , limits
            );
            out.insert(
                out.end(),
                std::make_move_iterator(timed.begin()),
                std::make_move_iterator(timed.end())
            );

            return out;
        }

        std::optional<DiscoveredConnection> complete_connection(
            const SearchBranch& branch
            , const PreprocessedNetwork& network
            , const SearchImpedance& impedance
            , const TransferLimits& limits
            , double fare_scale
        ) {
            if (!is_complete_connection(branch)) {
                return std::nullopt;
            }

            if (!limits.allow_end_wait
                && branch.last_timed_segment != nullptr
                && !branch.segments.empty()) {
                const auto& last_segment = connection_segment_at(
                    network,
                    branch.segments.back()
                );
                if (is_walk_connection(last_segment)) {
                    return std::nullopt;
                }
            }

            const auto destination = ZoneId{ branch.current_physical.id };
            const auto journey_time = Time{
                branch.current_time->value() - branch.departure->value()
            };
            return DiscoveredConnection{
                .origin = branch.origin
                , .destination = destination
                , .departure = *branch.departure
                , .arrival = *branch.current_time
                , .journey_time = journey_time
                , .transfer_time = branch.transfer_time
                , .transfers = branch.transfers
                , .fare = branch.fare
                , .impedance = branch_impedance_value(
                    journey_time
                    , branch.transfers
                    , branch.fare
                    , impedance
                    , fare_scale
                ),
                .segments = branch.segments
            };
        }

        bool accept_branch(
            const SearchBranch& branch
            , NodeConnectionMap& known_connections
            , const SearchParams& params
            , double fare_scale
        ) {
            if (!branch.departure.has_value() || !branch.current_time.has_value()) {
                return true;
            }

            const auto label = make_partial_label(branch, params.impedance, fare_scale);
            const auto node = search_node_key(branch);
            auto it = known_connections.find(node);
            if (it == known_connections.end()) {
                insert_label(known_connections, node, label);
                return true;
            }

            if (!is_relevant(label, it->second)) {
                return false;
            }

            if (!within_search_tolerances(
                label
                , it->second.summary
                , params.search_tolerances
                , params.transfers
            )) {
                return false;
            }

            insert_label(known_connections, node, label);
            return true;
        }

        BranchState feasibility_state(
            const SearchBranch& branch
        ) noexcept {
            return BranchState{
                .start_time = std::nullopt
                , .current_arrival_time = branch.current_time
                , .last_segment = branch.last_timed_segment
                , .last_route_segment = branch.last_timed_route_segment
                , .transfer_count = branch.departure.has_value()
                    ? std::optional<TransferCount>{ branch.transfers }
                    : std::nullopt
            };
        }

        mathfp::Expected<std::vector<DiscoveredConnection>> search_from_origin(
            ZoneId origin
            , const PreprocessedNetwork& network
            , double fare_scale
            , const SearchParams& params
        ) {
            std::vector<DiscoveredConnection> found;
            NodeConnectionMap known_connections;

            std::deque<SearchBranch> current_frontier;
            std::deque<SearchBranch> next_frontier;
            current_frontier.push_back(
                SearchBranch{
                    .origin = origin
                    , .current_physical = endpoint_key(origin)
                    , .current_occurrence = std::nullopt
                    , .departure = std::nullopt
                    , .current_time = std::nullopt
                    , .walk_time = Time{ 0.0 }
                    , .access_walk_time = Time{ 0.0 }
                    , .transfer_time = Time{ 0.0 }
                    , .transfers = TransferCount{ 0 }
                    , .fare = 0.0
                    , .segments = {}
                    , .visited_physical = { endpoint_key(origin) }
                    , .visited_occurrences = {}
                    , .last_timed_segment = nullptr
                    , .last_timed_route_segment = nullptr
                }
            );

            while (!current_frontier.empty() || !next_frontier.empty()) {
                if (current_frontier.empty()) {
                    current_frontier.swap(next_frontier);
                }

                auto branch = std::move(current_frontier.front());
                current_frontier.pop_front();

                if (is_complete_connection(branch)) {
                    if (const auto complete = complete_connection(
                        branch, network, params.impedance, params.transfers, fare_scale
                    ); complete.has_value()) {
                        found.push_back(std::move(*complete));
                    }
                    continue;
                }

                for (const auto successor_id : successor_ids(network, branch, params.transfers)) {
                    const auto& successor = connection_segment_at(network, successor_id);
                    const auto& successor_route_segment = route_segment_at(
                        network
                        , successor.route_segment
                    );
                    if (!is_branch_extension_feasible(
                        feasibility_state(branch)
                        , successor
                        , successor_route_segment
                        , params.transfers
                    )) {
                        continue;
                    }
                    if (!improves_repeated_stop_reboarding(
                        branch
                        , network
                        , successor
                        , successor_route_segment
                    )) {
                        continue;
                    }

                    auto candidate = extend_branch(branch, network, successor);
                    if (!candidate.has_value()) {
                        continue;
                    }

                    if (candidate->transfers > params.transfers.max_transfers) {
                        continue;
                    }

                    if (!accept_branch(
                        *candidate
                        , known_connections
                        , params
                        , fare_scale
                    )) {
                        continue;
                    }

                    const auto same_level =
                        is_walk_connection(successor) || !branch.departure.has_value();
                    if (same_level) {
                        current_frontier.push_back(std::move(*candidate));
                    } else {
                        next_frontier.push_back(std::move(*candidate));
                    }
                }
            }

            return found;
        }

        struct OdKey final {
            ZoneId origin{};
            ZoneId destination{};

            auto operator<=>(const OdKey&) const = default;
        };

        struct ChoiceGroupStats final {
            double min_impedance{ std::numeric_limits<double>::infinity() };
            double min_journey_time{ std::numeric_limits<double>::infinity() };
            double min_transfers{ std::numeric_limits<double>::infinity() };
        };

        OdKey od_key(const DiscoveredConnection& connection) noexcept {
            return OdKey{
                .origin = connection.origin,
                .destination = connection.destination
            };
        }

        bool choice_dominates(
            const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
        ) noexcept {
            const auto no_worse =
                lhs.departure.value() >= rhs.departure.value()
                && lhs.arrival.value() <= rhs.arrival.value()
                && lhs.impedance <= rhs.impedance
                && lhs.transfers.get() <= rhs.transfers.get();

            const auto strictly_better =
                lhs.departure.value() > rhs.departure.value()
                || lhs.arrival.value() < rhs.arrival.value()
                || lhs.impedance < rhs.impedance
                || lhs.transfers.get() < rhs.transfers.get();

            return no_worse && strictly_better;
        }

        bool is_choice_relevant(
            std::span<const DiscoveredConnection> connections
            , std::size_t candidate_index
        ) noexcept {
            const auto& candidate = connections[candidate_index];
            for (std::size_t i = 0; i < connections.size(); ++i) {
                if (i == candidate_index) {
                    continue;
                }
                if (choice_dominates(connections[i], candidate)) {
                    return false;
                }
            }
            return true;
        }

        ChoiceGroupStats collect_choice_group_stats(
            std::span<const DiscoveredConnection> connections
        ) noexcept {
            ChoiceGroupStats stats;
            for (const auto& connection : connections) {
                stats.min_impedance = std::min(stats.min_impedance, connection.impedance);
                stats.min_journey_time = std::min(
                    stats.min_journey_time
                    , connection.journey_time.value()
                );
                stats.min_transfers = std::min(
                    stats.min_transfers
                    , static_cast<double>(connection.transfers.get())
                );
            }
            return stats;
        }

        bool within_choice_tolerances(
            const DiscoveredConnection& connection
            , const ChoiceGroupStats& stats
            , const ChoiceTolerances& tolerances
        ) noexcept {
            return connection.impedance
                    <= mathfp::units::as_dimless(tolerances.imp_mult) * stats.min_impedance
                        + mathfp::units::as_dimless(tolerances.imp_add)
                && connection.journey_time.value()
                    <= mathfp::units::as_dimless(tolerances.jt_mult) * stats.min_journey_time
                        + mathfp::units::as_dimless(tolerances.jt_add)
                && static_cast<double>(connection.transfers.get())
                    <= mathfp::units::as_dimless(tolerances.nt_mult) * stats.min_transfers
                        + mathfp::units::as_dimless(tolerances.nt_add);
        }

        std::vector<DiscoveredConnection> filter_choice_group(
            std::vector<DiscoveredConnection> connections
            , const ChoiceTolerances& tolerances
        ) {
            std::vector<DiscoveredConnection> relevant;
            relevant.reserve(connections.size());
            for (std::size_t i = 0; i < connections.size(); ++i) {
                if (is_choice_relevant(connections, i)) {
                    relevant.push_back(std::move(connections[i]));
                }
            }

            const auto stats = collect_choice_group_stats(relevant);
            std::vector<DiscoveredConnection> chosen;
            chosen.reserve(relevant.size());
            for (auto& connection : relevant) {
                if (within_choice_tolerances(connection, stats, tolerances)) {
                    chosen.push_back(std::move(connection));
                }
            }

            std::sort(
                chosen.begin(),
                chosen.end(),
                [](const DiscoveredConnection& lhs, const DiscoveredConnection& rhs) {
                    if (lhs.departure != rhs.departure) {
                        return lhs.departure.value() < rhs.departure.value();
                    }
                    if (lhs.arrival != rhs.arrival) {
                        return lhs.arrival.value() < rhs.arrival.value();
                    }
                    if (lhs.impedance != rhs.impedance) {
                        return lhs.impedance < rhs.impedance;
                    }
                    if (lhs.transfers != rhs.transfers) {
                        return lhs.transfers.get() < rhs.transfers.get();
                    }
                    return lhs.segments < rhs.segments;
                }
            );

            return chosen;
        }

        std::vector<std::pair<OdKey, std::vector<DiscoveredConnection>>> group_connections_by_od(
            std::span<const DiscoveredConnection> connections
        ) {
            std::vector<std::pair<OdKey, std::vector<DiscoveredConnection>>> groups;
            for (const auto& connection : connections) {
                const auto key = od_key(connection);
                auto it = std::find_if(
                    groups.begin()
                    , groups.end()
                    , [&](const auto& group) { return group.first == key; }
                );
                if (it == groups.end()) {
                    groups.push_back({ key, {} });
                    it = std::prev(groups.end());
                }
                it->second.push_back(connection);
            }

            std::sort(
                groups.begin()
                , groups.end()
                , [](const auto& lhs, const auto& rhs) {
                    if (lhs.first.origin != rhs.first.origin) {
                        return lhs.first.origin.get() < rhs.first.origin.get();
                    }
                    return lhs.first.destination.get() < rhs.first.destination.get();
                }
            );
            return groups;
        }

        using ChoiceGroups = std::map<OdKey, std::vector<DiscoveredConnection>>;

        ChoiceGroups choice_groups_by_od(
            std::span<const DiscoveredConnection> connections
        ) {
            ChoiceGroups groups;
            for (const auto& connection : connections) {
                groups[od_key(connection)].push_back(connection);
            }
            return groups;
        }

        double perceived_journey_time(
            const DiscoveredConnection& connection
        ) noexcept {
            return connection.journey_time.value()
                + 2.0 * connection.transfer_time.value()
                + 2.0 * static_cast<double>(connection.transfers.get());
        }

        double temporal_utility(
            const DiscoveredConnection& connection
            , const TimeInterval& interval
        ) noexcept {
            const auto dep = connection.departure.value();
            if (dep < interval.start.value()) {
                return interval.start.value() - dep;
            }
            if (dep > interval.end.value()) {
                return dep - interval.end.value();
            }
            return 0.0;
        }

        double split_impedance(
            const DiscoveredConnection& connection
            , const TimeInterval& interval
            , const SplitParams& params
        ) noexcept {
            return mathfp::units::as_dimless(params.q_time) * perceived_journey_time(connection)
                + mathfp::units::as_dimless(params.q_departure) * temporal_utility(connection, interval)
                + mathfp::units::as_dimless(params.q_fare) * connection.fare;
        }

        double box_cox_transform(
            double value
            , double t
        ) noexcept {
            const auto positive = std::max(value, numeric::positive_stability_floor());
            if (mathfp::almost_zero(t)) {
                return std::log(positive);
            }
            return (std::pow(positive, t) - 1.0) / t;
        }

        double temporal_similarity(
            const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
        ) noexcept {
            return 0.5 * (
                std::abs(rhs.departure.value() - lhs.departure.value())
                + std::abs(rhs.arrival.value() - lhs.arrival.value())
            );
        }

        double journey_advantage(
            const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
        ) noexcept {
            return perceived_journey_time(rhs) - perceived_journey_time(lhs);
        }

        double fare_advantage(
            const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
        ) noexcept {
            return rhs.fare - lhs.fare;
        }

        double asymmetric_quality_scale(
            double advantage
            , const SplitParams& params
        ) noexcept {
            // Paper pages 11-12: s_y / s_z depend on the sign of the advantage,
            // so superior base connections are affected less than inferior ones.
            return advantage >= 0.0
                ? mathfp::units::as_dimless(params.higher_quality_scale)
                : mathfp::units::as_dimless(params.lower_quality_scale);
        }

        double normalized_quality_distance(
            double advantage
            , const SplitParams& params
        ) noexcept {
            const auto scale = asymmetric_quality_scale(advantage, params);
            if (scale <= 0.0) {
                return 0.0;
            }
            return std::abs(advantage) / scale;
        }

        double capped_proximity(
            double similarity
            , double scale
        ) noexcept {
            if (scale <= 0.0) {
                return similarity <= 0.0 ? 1.0 : 0.0;
            }
            return 1.0 - std::min(1.0, similarity / scale);
        }

        double connection_influence(
            const DiscoveredConnection& base
            , const DiscoveredConnection& other
            , const SplitParams& params
        ) noexcept {
            const auto x = temporal_similarity(base, other);
            const auto y = journey_advantage(base, other);
            const auto z = fare_advantage(base, other);
            const auto proximity = capped_proximity(
                x
                , mathfp::units::as_dimless(params.temporal_similarity_scale)
            );
            const auto y_term = normalized_quality_distance(y, params);
            const auto z_term = normalized_quality_distance(z, params);

            return proximity * std::exp(-mathfp::units::as_dimless(params.gamma) * (y_term + z_term));
        }

        double connection_independence(
            std::span<const DiscoveredConnection> connections
            , std::size_t index
            , const SplitParams& params
        ) noexcept {
            double influence_sum = 0.0;
            for (std::size_t i = 0; i < connections.size(); ++i) {
                if (i == index) {
                    continue;
                }
                influence_sum += connection_influence(connections[index], connections[i], params);
            }
            return 1.0 / (1.0 + influence_sum);
        }

        const TimeInterval* find_interval(
            const InputModel& input
            , IntervalId id
        ) noexcept {
            const auto it = std::find_if(
                input.intervals.begin()
                , input.intervals.end()
                , [&](const TimeInterval& interval) { return interval.id == id; }
            );
            return it == input.intervals.end() ? nullptr : &*it;
        }

    }  // namespace

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const PreprocessedNetwork& network
        , double fare_scale
        , const SearchParams& params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("search: branch-and-bound");
        log(
            fmt::format(
                "search input: route_segments = {:>8}  connection_segments = {:>8}"
                "  max_transfers = {}"
                , network.route_segments.size()
                , network.connection_segments.size()
                , params.transfers.max_transfers.get()
            ),
            LogLevel::Info
        );

        const auto origins = search_origins(network);
        ConnectionSearchResult result;

        for (const auto origin : origins) {
            MATHFP_TRY_LET(
                std::vector<DiscoveredConnection>
                , origin_connections
                , search_from_origin(origin, network, fare_scale, params)
            );
            result.connections.insert(
                result.connections.end()
                , std::make_move_iterator(origin_connections.begin())
                , std::make_move_iterator(origin_connections.end())
            );
        }

        log(
            fmt::format(
                "search result: origins = {:>6}  connections = {:>8}"
                , origins.size()
                , result.connections.size()
            )
            , LogLevel::Info
        );
        both("search: branch-and-bound done");
        return result;
    }

    mathfp::Expected<ConnectionChoiceResult> choose_connections(
        const ConnectionSearchResult& search_result
        , const SearchParams& params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("choice: pruning connections");
        log(
            fmt::format(
                "choice input: connections = {:>8}"
                , search_result.connections.size()
            )
            , LogLevel::Info
        );

        ConnectionChoiceResult result;
        const auto groups = group_connections_by_od(search_result.connections);
        for (const auto& [key, group_connections] : groups) {
            auto chosen = filter_choice_group(group_connections, params.choice_tolerances);
            log(
                fmt::format(
                    "choice group: origin = {:>6}  destination = {:>6}"
                    "  input = {:>5}  chosen = {:>5}"
                    , key.origin.get()
                    , key.destination.get()
                    , group_connections.size()
                    , chosen.size()
                ),
                LogLevel::Info
            );
            result.connections.insert(
                result.connections.end()
                , std::make_move_iterator(chosen.begin())
                , std::make_move_iterator(chosen.end())
            );
        }

        log(
            fmt::format(
                "choice result: groups = {:>6}  connections = {:>8}"
                , groups.size()
                , result.connections.size()
            )
            , LogLevel::Info
        );
        both("choice: pruning connections done");
        return result;
    }

    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
        const ConnectionChoiceResult& choice_result
        , const InputModel& input
        , const SearchParams& params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("split: demand assignment");
        log(
            fmt::format(
                "split input: chosen_connections = {:>8}  demand_entries = {:>8}"
                , choice_result.connections.size()
                , input.demand.size()
            )
            , LogLevel::Info
        );

        DemandSplitResult result;
        const auto groups = choice_groups_by_od(choice_result.connections);
        const auto beta = mathfp::units::as_dimless(params.split.beta);
        const auto boxcox_t = mathfp::units::as_dimless(params.split.boxcox_t);

        for (const auto& demand : input.demand) {
            const auto interval = find_interval(input, demand.interval);
            if (!interval) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("demand entry references unknown time interval")
                        .ctx("interval_id", demand.interval.get())
                );
            }

            const auto it = groups.find(OdKey{ demand.origin, demand.destination });
            if (it == groups.end() || it->second.empty() || demand.passengers <= 0.0) {
                continue;
            }

            const auto& connections = it->second;
            std::vector<double> independences;
            std::vector<double> split_impedances;
            std::vector<double> log_weights;
            independences.reserve(connections.size());
            split_impedances.reserve(connections.size());
            log_weights.reserve(connections.size());

            double max_log_weight = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < connections.size(); ++i) {
                const auto independence = connection_independence(
                    connections
                    , i
                    , params.split
                );
                const auto imp = split_impedance(
                    connections[i]
                    , *interval
                    , params.split
                );
                const auto transformed = box_cox_transform(imp, boxcox_t);
                const auto log_weight = std::log(std::max(
                    independence
                    , numeric::positive_stability_floor()
                ))
                    - beta * transformed;

                independences.push_back(independence);
                split_impedances.push_back(imp);
                log_weights.push_back(log_weight);
                max_log_weight = std::max(max_log_weight, log_weight);
            }

            double weight_sum = 0.0;
            std::vector<double> weights;
            weights.reserve(log_weights.size());
            for (const auto log_weight : log_weights) {
                const auto weight = std::exp(log_weight - max_log_weight);
                weights.push_back(weight);
                weight_sum += weight;
            }

            if (!(weight_sum > 0.0) || !std::isfinite(weight_sum)) {
                return mathfp::unexpected(
                    mathfp::domain_error("invalid split weight normalization")
                        .ctx("origin", demand.origin.get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval", demand.interval.get())
                );
            }

            for (std::size_t i = 0; i < connections.size(); ++i) {
                const auto probability = weights[i] / weight_sum;
                result.shares.push_back(
                    ConnectionDemandShare{
                        .origin = demand.origin
                        , .destination = demand.destination
                        , .interval = demand.interval
                        , .connection = connections[i]
                        , .passengers = demand.passengers * probability
                        , .probability = probability
                        , .independence = independences[i]
                        , .split_impedance = split_impedances[i]
                    }
                );
            }
        }

        log(
            fmt::format(
                "split result: shares = {:>8}"
                , result.shares.size()
            )
            , LogLevel::Info
        );
        both("split: demand assignment done");
        return result;
    }

}  // namespace timetable::domain::assignment
