#include "timetable/domain/assignment/search/search.hpp"

#include <algorithm>
#include <cstddef>
#include <deque>
#include <iterator>
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
#include "timetable/domain/assignment/search/branch_state.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        struct PartialConnectionLabel final {
            Time          departure{};
            Time          arrival{};
            Time          journey_time{};
            // Sum of walk-segment durations accumulated along the partial connection.
            Time          walk_time{};
            TransferCount transfers{};
            double        fare{};
            double        impedance{};
        };

        struct NodeLocalSearchSummary final {
            double min_impedance   { std::numeric_limits<double>::infinity() };
            double min_journey_time{ std::numeric_limits<double>::infinity() };
            // Paper-consistent node-local summary values retained alongside the
            // current relevance/tolerance checks.
            double min_walk_time   { std::numeric_limits<double>::infinity() };
            double min_transfers   { std::numeric_limits<double>::infinity() };
            double min_fare        { std::numeric_limits<double>::infinity() };
        };

        struct NodeConnectionSet final {
            std::vector<PartialConnectionLabel> labels{};
            NodeLocalSearchSummary              summary{};
        };

        struct SearchNodeKey final {
            EndpointKey                      physical{};
            std::optional<StopOccurrenceKey> occurrence{};

            auto operator<=>(const SearchNodeKey&) const = default;
        };

        struct SearchNodeKeyHash final {
            std::size_t operator()(const SearchNodeKey& key) const noexcept {
                std::size_t seed = 17u;
                seed             = seed * 31u + std::hash<std::int64_t>{}(static_cast<std::int64_t>(key.physical.kind));
                seed             = seed * 31u + std::hash<std::int64_t>{}(key.physical.id);
                seed             = seed * 31u + std::hash<bool>{}(key.occurrence.has_value());
                if (key.occurrence.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->stop.get());
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->position.get());
                }
                return seed;
            }
        };

        struct SearchBranch final {
            ZoneId                           origin{};
            EndpointKey                      current_physical{};
            std::optional<StopOccurrenceKey> current_occurrence{};
            std::optional<Time>              departure{};
            std::optional<Time>              current_time{};
            // Total accumulated walk duration over all walk segments seen so far.
            Time                             walk_time{};
            Time                             access_walk_time{};
            Time                             transfer_time{};
            TransferCount                    transfers{};
            double                           fare{};
            std::optional<std::size_t>       parent_branch{};
            std::optional<ConnectionSegmentId> incoming_segment{};
            const ConnectionSegment*         last_timed_segment{};
            const RouteSegment*              last_timed_route_segment{};
        };

        struct OriginSearchStats final {
            std::size_t expanded_branches{};
            std::size_t generated_successors{};
            std::size_t accepted_branches{};
            std::size_t rejected_feasibility{};
            std::size_t rejected_reboarding{};
            std::size_t rejected_cycles{};
            std::size_t rejected_transfer_limit{};
            std::size_t rejected_dominance_or_tolerance{};
            std::size_t completed_connections{};
            std::size_t max_current_frontier{};
            std::size_t max_next_frontier{};
        };

        using NodeConnectionMap = std::unordered_map<SearchNodeKey, NodeConnectionSet, SearchNodeKeyHash>;
        using BranchArena = std::vector<SearchBranch>;

        constexpr std::size_t kOriginProgressStep = 10;
        constexpr std::size_t kSearchHeartbeatStep = 100'000;

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

        double branch_impedance_value(
              Time                   journey_time
            , TransferCount          transfers
            , double                 fare
            , const SearchImpedance& impedance
            , double                 fare_scale
        ) noexcept {
            return connection_impedance_value(
                  journey_time
                , transfers
                , fare
                , impedance
                , fare_scale
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
                  .physical   = branch.current_physical
                , .occurrence = branch.current_occurrence
            };
        }

        bool branch_revisits_physical(
              const BranchArena&  branches
            , const SearchBranch& branch
            , EndpointKey         next
        ) {
            if (branch.current_physical == next) {
                return true;
            }

            auto cursor = branch.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branches[*cursor];
                if (ancestor.current_physical == next) {
                    return true;
                }
                cursor = ancestor.parent_branch;
            }

            return false;
        }

        bool branch_revisits_occurrence(
              const BranchArena&     branches
            , const SearchBranch&    branch
            , StopOccurrenceKey   next
        ) {
            if (branch.current_occurrence.has_value() && branch.current_occurrence.value() == next) {
                return true;
            }

            auto cursor = branch.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branches[*cursor];
                if (ancestor.current_occurrence.has_value() && ancestor.current_occurrence.value() == next) {
                    return true;
                }
                cursor = ancestor.parent_branch;
            }

            return false;
        }

        bool is_complete_connection(
            const SearchBranch& branch
        ) noexcept {
            return branch.departure.has_value()
                && branch.current_physical.kind == EndpointKind::Zone
                && branch.current_physical.id != branch.origin.get();
        }

        PartialConnectionLabel make_partial_label(
              const SearchBranch&    branch
            , const SearchImpedance& impedance
            , double                 fare_scale
        ) {
            const auto journey_time = Time{
                branch.current_time->value() - branch.departure->value()
            };

            return PartialConnectionLabel{
                  .departure    = *branch.departure
                , .arrival      = *branch.current_time
                , .journey_time = journey_time
                , .walk_time    = branch.walk_time
                , .transfers    = branch.transfers
                , .fare         = branch.fare
                , .impedance    = branch_impedance_value(
                      journey_time
                    , branch.transfers
                    , branch.fare
                    , impedance
                    , fare_scale
                )
            };
        }

        bool dominates(
              const PartialConnectionLabel& lhs
            , const PartialConnectionLabel& rhs
        ) noexcept {
            return lhs.departure.value() >= rhs.departure.value()
                && lhs.arrival.value()   <= rhs.arrival.value()
                && lhs.impedance          <= rhs.impedance
                && lhs.transfers.get()    <= rhs.transfers.get();
        }

        bool is_relevant(
              const PartialConnectionLabel& candidate
            , const NodeConnectionSet&      known
        ) noexcept {
            for (const auto& existing : known.labels) {
                if (dominates(existing, candidate)) {
                    return false;
                }
            }
            return true;
        }

        bool within_search_tolerances(
              const PartialConnectionLabel& candidate
            , const NodeLocalSearchSummary& summary
            , const SearchTolerances&       tolerances
            , const TransferLimits&         limits
        ) noexcept {
            return candidate.transfers <= limits.max_transfers
                && candidate.impedance
                       <= mathfp::units::as_dimless(tolerances.imp_mult) * summary.min_impedance
                           + mathfp::units::as_dimless(tolerances.imp_add)
                && candidate.journey_time.value()
                       <= mathfp::units::as_dimless(tolerances.jt_mult) * summary.min_journey_time
                           + mathfp::units::as_dimless(tolerances.jt_add)
                && static_cast<double>(candidate.transfers.get())
                       <= mathfp::units::as_dimless(tolerances.nt_mult) * summary.min_transfers
                           + mathfp::units::as_dimless(tolerances.nt_add);
        }

        void update_node_local_search_summary(
              NodeLocalSearchSummary&       summary
            , const PartialConnectionLabel& label
        ) noexcept {
            summary.min_impedance    = std::min(summary.min_impedance, label.impedance);
            summary.min_journey_time = std::min(summary.min_journey_time, label.journey_time.value());
            summary.min_walk_time    = std::min(summary.min_walk_time, label.walk_time.value());
            summary.min_transfers    = std::min(
                  summary.min_transfers
                , static_cast<double>(label.transfers.get())
            );
            summary.min_fare = std::min(summary.min_fare, label.fare);
        }

        NodeLocalSearchSummary summarize_labels(
            std::span<const PartialConnectionLabel> labels
        ) noexcept {
            NodeLocalSearchSummary summary{};
            for (const auto& label : labels) {
                update_node_local_search_summary(summary, label);
            }
            return summary;
        }

        void insert_label(
              NodeConnectionMap&     known_connections
            , SearchNodeKey          node
            , PartialConnectionLabel label
        ) {
            auto& known = known_connections[node];
            known.labels.erase(
                std::remove_if(
                      known.labels.begin()
                    , known.labels.end()
                    , [&](const PartialConnectionLabel& existing) {
                        return dominates(label, existing);
                    }
                )
              , known.labels.end()
            );

            const auto insertion = std::lower_bound(
                  known.labels.begin()
                , known.labels.end()
                , label.arrival.value()
                , [](const PartialConnectionLabel& lhs, double arrival_value) {
                    return lhs.arrival.value() < arrival_value;
                }
            );
            known.labels.insert(insertion, std::move(label));
            known.summary = summarize_labels(known.labels);
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
              const SearchBranch&      branch
            , const ConnectionSegment& successor
            , const RouteSegment&      successor_route_segment
        ) noexcept {
            if (!is_same_line_transfer_candidate(branch, successor, successor_route_segment)) {
                return false;
            }
            const auto* current_line   = line_topology_of(*branch.last_timed_route_segment);
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
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , const RouteSegment&        successor_route_segment
        ) noexcept {
            if (!branch.last_timed_segment || !branch.last_timed_route_segment) {
                return std::nullopt;
            }
            if (!branch.last_timed_segment->trip.has_value()
                || !branch.last_timed_segment->to_index.has_value()
                || !successor.to_index.has_value()) {
                return std::nullopt;
            }

            const auto current_stop           = occurrence_key(line_topology_of(*branch.last_timed_route_segment)->to);
            const auto desired_route_to_index = successor.to_index.value();
            const auto range                  = timed_bucket_range(network.connection_index, current_stop);
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

        SearchBranch extend_with_walk(
              const SearchBranch& branch
            , std::size_t         branch_index
            , const RouteSegment& route_segment
            , ConnectionSegmentId segment_id
        ) {
            SearchBranch next = branch;
            next.parent_branch     = branch_index;
            next.incoming_segment  = segment_id;
            next.current_physical  = physical_to_key(route_segment);
            next.current_occurrence = std::nullopt;
            next.walk_time          = Time{
                branch.walk_time.value() + route_segment.run_time.value()
            };

            if (branch.departure.has_value()) {
                next.current_time = Time{
                    branch.current_time->value() + route_segment.run_time.value()
                };
                if (next.current_physical.kind == EndpointKind::Stop) {
                    next.transfer_time = Time{
                        branch.transfer_time.value() + route_segment.run_time.value()
                    };
                }
            } else {
                next.access_walk_time = Time{
                    branch.access_walk_time.value() + route_segment.run_time.value()
                };
            }

            return next;
        }

        SearchBranch extend_with_timed(
              const SearchBranch&       branch
            , std::size_t               branch_index
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
        ) {
            SearchBranch next = branch;
            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            const auto had_departure   = branch.departure.has_value();
            next.parent_branch         = branch_index;
            next.incoming_segment      = segment.id;
            if (!had_departure) {
                next.departure = Time{
                    segment.departure->value() - branch.access_walk_time.value()
                };
            } else {
                next.transfer_time = Time{
                    branch.transfer_time.value()
                    + (segment.departure->value() - branch.current_time->value())
                };
                next.transfers = TransferCount{ branch.transfers.get() + 1 };
            }

            next.current_time            = *segment.arrival;
            next.current_physical        = physical_to_key(route_segment);
            next.current_occurrence      = next_occurrence;
            next.fare                    = branch.fare + segment.fare.value_or(0.0);
            next.last_timed_segment      = &segment;
            next.last_timed_route_segment = &route_segment;
            return next;
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
                , branch.current_physical
            );

            for (const auto connection_id : lookup.walk_connections) {
                visitor(connection_id);
            }

            for_each_timed_successor(
                  network
                , branch.current_physical
                , branch.current_time
                , limits
                , visitor
            );
        }

        std::vector<ConnectionSegmentId> reconstruct_segment_trace(
              const BranchArena&  branches
            , const SearchBranch& branch
        ) {
            std::vector<ConnectionSegmentId> reversed;

            auto cursor_branch = &branch;
            while (cursor_branch != nullptr) {
                if (cursor_branch->incoming_segment.has_value()) {
                    reversed.push_back(cursor_branch->incoming_segment.value());
                }

                if (!cursor_branch->parent_branch.has_value()) {
                    break;
                }
                cursor_branch = &branches[*cursor_branch->parent_branch];
            }

            std::reverse(reversed.begin(), reversed.end());
            return reversed;
        }

        std::optional<DiscoveredConnection> complete_connection(
              const BranchArena&         branches
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const SearchImpedance&     impedance
            , const TransferLimits&      limits
            , double                     fare_scale
        ) {
            if (!is_complete_connection(branch)) {
                return std::nullopt;
            }

            if (!limits.allow_end_wait
                && branch.last_timed_segment != nullptr
                && branch.incoming_segment.has_value()) {
                const auto& last_segment = connection_segment_at(
                      network
                    , branch.incoming_segment.value()
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
                  .origin        = branch.origin
                , .destination   = destination
                , .departure     = *branch.departure
                , .arrival       = *branch.current_time
                , .journey_time  = journey_time
                , .transfer_time = branch.transfer_time
                , .transfers     = branch.transfers
                , .fare          = branch.fare
                , .impedance     = branch_impedance_value(
                      journey_time
                    , branch.transfers
                    , branch.fare
                    , impedance
                    , fare_scale
                )
                , .segments = reconstruct_segment_trace(branches, branch)
            };
        }

        bool accept_branch(
              const SearchBranch& branch
            , NodeConnectionMap&  known_connections
            , const SearchParams& params
            , double              fare_scale
        ) {
            if (!branch.departure.has_value() || !branch.current_time.has_value()) {
                return true;
            }

            const auto label = make_partial_label(branch, params.impedance, fare_scale);
            const auto node  = search_node_key(branch);
            auto it          = known_connections.find(node);
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
                  .start_time           = std::nullopt
                , .current_arrival_time = branch.current_time
                , .last_segment         = branch.last_timed_segment
                , .last_route_segment   = branch.last_timed_route_segment
                , .transfer_count       = branch.departure.has_value()
                    ? std::optional<TransferCount>{ branch.transfers }
                    : std::nullopt
            };
        }

        mathfp::Expected<std::vector<DiscoveredConnection>> search_from_origin(
              ZoneId                     origin
            , const PreprocessedNetwork& network
            , double                     fare_scale
            , const SearchParams&        params
            , std::size_t                origin_index
            , std::size_t                origin_count
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            std::vector<DiscoveredConnection> found;
            NodeConnectionMap known_connections;
            OriginSearchStats stats;
            BranchArena branches;
            branches.reserve(network.connection_segments.size() / 4 + 1);

            std::deque<std::size_t> current_frontier;
            std::deque<std::size_t> next_frontier;
            branches.push_back(
                SearchBranch{
                      .origin                   = origin
                    , .current_physical         = endpoint_key(origin)
                    , .current_occurrence       = std::nullopt
                    , .departure                = std::nullopt
                    , .current_time             = std::nullopt
                    , .walk_time                = Time{ 0.0 }
                    , .access_walk_time         = Time{ 0.0 }
                    , .transfer_time            = Time{ 0.0 }
                    , .transfers                = TransferCount{ 0 }
                    , .fare                     = 0.0
                    , .parent_branch            = std::nullopt
                    , .incoming_segment         = std::nullopt
                    , .last_timed_segment       = nullptr
                    , .last_timed_route_segment = nullptr
                }
            );
            current_frontier.push_back(0);

            status(
                fmt::format(
                      "search: origin {}/{} zone={} frontier={} found={}"
                    , origin_index + 1
                    , origin_count
                    , origin.get()
                    , current_frontier.size()
                    , found.size()
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
                              "search: origin {}/{} zone={} expanded={} accepted={} found={} frontier={}/{}"
                            , origin_index + 1
                            , origin_count
                            , origin.get()
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , found.size()
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                    );
                    log(
                        fmt::format(
                              "search heartbeat: origin={:>4} expanded={:>8} generated={:>8}"
                              " accepted={:>8} found={:>8} rejected(feasibility/reboarding/cycles/limit/dominance)={}/{}/{}/{}/{}"
                              " frontier={}/{}"
                            , origin.get()
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , found.size()
                            , stats.rejected_feasibility
                            , stats.rejected_reboarding
                            , stats.rejected_cycles
                            , stats.rejected_transfer_limit
                            , stats.rejected_dominance_or_tolerance
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                        , LogLevel::Info
                    );
                }

                if (is_complete_connection(branch)) {
                    if (const auto complete = complete_connection(
                        branches, branch, network, params.impedance, params.transfers, fare_scale
                    ); complete.has_value()) {
                        found.push_back(std::move(*complete));
                        ++stats.completed_connections;
                    }
                    continue;
                }

                for_each_successor(network, branch, params.transfers, [&](ConnectionSegmentId successor_id) {
                    ++stats.generated_successors;
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

                    if (candidate->transfers > params.transfers.max_transfers) {
                        ++stats.rejected_transfer_limit;
                        return;
                    }

                    if (!accept_branch(
                          *candidate
                        , known_connections
                        , params
                        , fare_scale
                    )) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }
                    ++stats.accepted_branches;
                    const auto same_level =
                        is_walk_connection(successor) || !branch.departure.has_value();
                    branches.push_back(std::move(*candidate));
                    const auto candidate_index = branches.size() - 1;
                    if (same_level) {
                        current_frontier.push_back(candidate_index);
                    } else {
                        next_frontier.push_back(candidate_index);
                    }
                });
            }

            log(
                fmt::format(
                      "search origin done: {}/{} zone={} found={:>8} expanded={:>8} generated={:>8} accepted={:>8}"
                      " rejected(feasibility/reboarding/cycles/limit/dominance)={}/{}/{}/{}/{} max_frontier={}/{}"
                    , origin_index + 1
                    , origin_count
                    , origin.get()
                    , found.size()
                    , stats.expanded_branches
                    , stats.generated_successors
                    , stats.accepted_branches
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

            return found;
        }

    }  // namespace

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , double                     fare_scale
        , const SearchParams&        params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        both("search: branch-and-bound");
        log(
            fmt::format(
                "search input: route_segments = {:>8}  connection_segments = {:>8}"
                "  max_transfers = {}"
                , network.route_segments.size()
                , network.connection_segments.size()
                , params.transfers.max_transfers.get()
            )
            , LogLevel::Info
        );

        const auto origins = search_origins(network);
        ConnectionSearchResult result;

        log(
            fmt::format(
                  "search setup: origins = {:>6}  fare_scale = {:.6f}"
                , origins.size()
                , fare_scale
            )
            , LogLevel::Info
        );

        if (origins.empty()) {
            status("search: no origin zones available");
        }

        for (std::size_t i = 0; i < origins.size(); ++i) {
            const auto origin = origins[i];
            if (i == 0 || (i % kOriginProgressStep) == 0 || (i + 1) == origins.size()) {
                status(
                    fmt::format(
                          "search: origin {}/{} zone={} total_found={}"
                        , i + 1
                        , origins.size()
                        , origin.get()
                        , result.connections.size()
                    )
                );
            }
            log(
                fmt::format(
                      "search origin start: {}/{} zone={} cumulative_found={}"
                    , i + 1
                    , origins.size()
                    , origin.get()
                    , result.connections.size()
                )
                , LogLevel::Info
            );
            MATHFP_TRY_LET(
                  std::vector<DiscoveredConnection>
                , origin_connections
                , search_from_origin(origin, network, fare_scale, params, i, origins.size())
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

}  // namespace timetable::domain::assignment
