#include "timetable/domain/assignment/output.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {
    namespace {

        struct OdKey final {
            ZoneId origin{};
            ZoneId destination{};

            auto operator<=>(const OdKey&) const = default;
        };

        struct DemandKey final {
            ZoneId origin{};
            ZoneId destination{};
            IntervalId interval{};

            auto operator<=>(const DemandKey&) const = default;
        };

        struct ConnectionTraceKey final {
            ZoneId origin{};
            ZoneId destination{};
            std::vector<ConnectionSegmentId> segments{};

            auto operator<=>(const ConnectionTraceKey&) const = default;
        };

        OdKey od_key(
            const DiscoveredConnection& connection
        ) noexcept {
            return OdKey{
                .origin = connection.origin
                , .destination = connection.destination
            };
        }

        OdKey od_key(
            const DemandEntry& demand
        ) noexcept {
            return OdKey{
                .origin = demand.origin
                , .destination = demand.destination
            };
        }

        ConnectionTraceKey connection_trace_key(
            const DiscoveredConnection& connection
        ) {
            return ConnectionTraceKey{
                .origin = connection.origin
                , .destination = connection.destination
                , .segments = connection.segments
            };
        }

        DemandKey demand_key(
            const DemandEntry& demand
        ) noexcept {
            return DemandKey{
                .origin = demand.origin
                , .destination = demand.destination
                , .interval = demand.interval
            };
        }

        DemandKey demand_key(
            const ConnectionDemandShare& share
        ) noexcept {
            return DemandKey{
                .origin = share.origin
                , .destination = share.destination
                , .interval = share.interval
            };
        }

        mathfp::Expected<const TimeInterval*> find_interval(
            const InputModel& input
            , IntervalId interval_id
        ) {
            for (const auto& interval : input.intervals) {
                if (interval.id == interval_id) {
                    return &interval;
                }
            }

            return mathfp::unexpected(
                mathfp::internal_error("assignment output mapping references unknown interval")
                    .ctx("interval_id", interval_id.get())
            );
        }

        mathfp::Expected<AssignmentConnection> build_assignment_connection(
            const DiscoveredConnection& connection
            , const PreprocessedNetwork& network
        ) {
            AssignmentConnection output{
                .summary = connection
                , .segments = {}
            };
            output.segments.reserve(connection.segments.size());

            for (const auto segment_id : connection.segments) {
                const auto connection_segment_index = static_cast<std::size_t>(segment_id.get());
                if (connection_segment_index >= network.connection_segments.size()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output mapping references unknown connection segment")
                            .ctx("connection_segment_id" , segment_id.get())
                            .ctx("connection_origin"     , connection.origin.get())
                            .ctx("connection_destination", connection.destination.get())
                    );
                }

                const auto& connection_segment = network.connection_segments[connection_segment_index];
                const auto route_segment_index = static_cast<std::size_t>(connection_segment.route_segment.get());
                if (route_segment_index >= network.route_segments.size()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output mapping references unknown route segment")
                            .ctx("route_segment_id"     , connection_segment.route_segment.get())
                            .ctx("connection_segment_id", connection_segment.id.get())
                    );
                }

                output.segments.push_back(
                    AssignmentPathSegment{
                        .connection_segment = connection_segment
                        , .route_segment = network.route_segments[route_segment_index]
                    }
                );
            }

            return output;
        }

        std::map<OdKey, std::size_t> search_counts_by_od(
            const ConnectionSearchResult& search
        ) {
            std::map<OdKey, std::size_t> counts;
            for (const auto& connection : search.connections) {
                ++counts[od_key(connection)];
            }
            return counts;
        }

        std::map<OdKey, std::vector<const DiscoveredConnection*>> choice_connections_by_od(
            const ConnectionChoiceResult& choice
        ) {
            std::map<OdKey, std::vector<const DiscoveredConnection*>> grouped;
            for (const auto& connection : choice.connections) {
                grouped[od_key(connection)].push_back(&connection);
            }
            return grouped;
        }

        std::map<OdKey, std::vector<const DemandEntry*>> demand_entries_by_od(
            const InputModel& input
        ) {
            std::map<OdKey, std::vector<const DemandEntry*>> grouped;
            for (const auto& demand : input.demand) {
                grouped[od_key(demand)].push_back(&demand);
            }
            return grouped;
        }

        std::map<DemandKey, std::vector<const ConnectionDemandShare*>> shares_by_demand_key(
            const DemandSplitResult& split
        ) {
            std::map<DemandKey, std::vector<const ConnectionDemandShare*>> grouped;
            for (const auto& share : split.shares) {
                grouped[demand_key(share)].push_back(&share);
            }
            return grouped;
        }

        bool almost_equal_scalar(
            double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

        mathfp::Expected<mathfp::Unit> validate_od_result_semantics(
            const AssignmentOdResult& od_result
        ) {
            if (od_result.chosen_connection_count != od_result.connections.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output chosen_connection_count disagrees with connections size")
                        .ctx("origin"        , od_result.origin.get())
                        .ctx("destination"   , od_result.destination.get())
                        .ctx("declared_count", static_cast<std::int64_t>(od_result.chosen_connection_count))
                        .ctx("actual_count"  , static_cast<std::int64_t>(od_result.connections.size()))
                );
            }

            double demand_sum = 0.0;
            double assigned_sum = 0.0;
            for (const auto& interval : od_result.intervals) {
                double interval_assigned_sum = 0.0;
                for (const auto& share : interval.shares) {
                    const auto connection_index = share.connection_index.get();
                    if (connection_index < 0
                        || static_cast<std::size_t>(connection_index) >= od_result.connections.size()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("assignment output share references connection outside the OD-local connection set")
                                .ctx("origin"          , od_result.origin.get())
                                .ctx("destination"     , od_result.destination.get())
                                .ctx("interval_id"     , interval.interval.id.get())
                                .ctx("connection_index", connection_index)
                                .ctx("connection_count", static_cast<std::int64_t>(od_result.connections.size()))
                        );
                    }
                    interval_assigned_sum += share.passengers;
                }

                if (!almost_equal_scalar(interval.assigned_passengers, interval_assigned_sum)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output interval assigned_passengers disagrees with its shares")
                            .ctx("origin"           , od_result.origin.get())
                            .ctx("destination"      , od_result.destination.get())
                            .ctx("interval_id"      , interval.interval.id.get())
                            .ctx("declared_assigned", interval.assigned_passengers)
                            .ctx("actual_assigned"  , interval_assigned_sum)
                    );
                }

                demand_sum += interval.demand_passengers;
                assigned_sum += interval.assigned_passengers;
            }

            if (!almost_equal_scalar(od_result.total_demand_passengers, demand_sum)) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output OD total_demand_passengers disagrees with interval totals")
                        .ctx("origin"               , od_result.origin.get())
                        .ctx("destination"          , od_result.destination.get())
                        .ctx("declared_total_demand", od_result.total_demand_passengers)
                        .ctx("actual_total_demand"  , demand_sum)
                );
            }

            if (!almost_equal_scalar(od_result.assigned_passengers, assigned_sum)) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output OD assigned_passengers disagrees with interval totals")
                        .ctx("origin"           , od_result.origin.get())
                        .ctx("destination"      , od_result.destination.get())
                        .ctx("declared_assigned", od_result.assigned_passengers)
                        .ctx("actual_assigned"  , assigned_sum)
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_output_summary_semantics(
            const AssignmentOutput& output
        ) {
            std::size_t search_count = 0;
            std::size_t chosen_count = 0;
            std::size_t share_count  = 0;

            double      total_demand = 0.0;
            double      assigned     = 0.0;

            for (const auto& od_result : output.od_results) {
                MATHFP_TRY(validate_od_result_semantics(od_result));
                search_count += od_result.search_connection_count;
                chosen_count += od_result.chosen_connection_count;
                total_demand += od_result.total_demand_passengers;
                assigned += od_result.assigned_passengers;
                for (const auto& interval : od_result.intervals) {
                    share_count += interval.shares.size();
                }
            }

            if (output.summary.od_count != output.od_results.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output summary od_count disagrees with OD result count")
                        .ctx("declared_od_count", static_cast<std::int64_t>(output.summary.od_count))
                        .ctx("actual_od_count"  , static_cast<std::int64_t>(output.od_results.size()))
                );
            }

            if (output.summary.search_connection_count != search_count
                || output.summary.chosen_connection_count != chosen_count
                || output.summary.demand_share_count != share_count
                || !almost_equal_scalar(output.summary.total_demand_passengers, total_demand)
                || !almost_equal_scalar(output.summary.assigned_passengers, assigned)) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output summary disagrees with OD aggregates")
                        .ctx("declared_search_connections", static_cast<std::int64_t>(output.summary.search_connection_count))
                        .ctx("actual_search_connections"  , static_cast<std::int64_t>(search_count))
                        .ctx("declared_chosen_connections", static_cast<std::int64_t>(output.summary.chosen_connection_count))
                        .ctx("actual_chosen_connections"  , static_cast<std::int64_t>(chosen_count))
                        .ctx("declared_share_count"       , static_cast<std::int64_t>(output.summary.demand_share_count))
                        .ctx("actual_share_count"         , static_cast<std::int64_t>(share_count))
                        .ctx("declared_total_demand"      , output.summary.total_demand_passengers)
                        .ctx("actual_total_demand"        , total_demand)
                        .ctx("declared_assigned"          , output.summary.assigned_passengers)
                        .ctx("actual_assigned"            , assigned)
                );
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<AssignmentOutput> build_assignment_output(
        const InputModel& input
        , const PreprocessedNetwork& network
        , const ConnectionSearchResult& search_result
        , const ConnectionChoiceResult& choice_result
        , const DemandSplitResult& split_result
    ) {
        const auto search_counts = search_counts_by_od(search_result);
        const auto chosen_by_od  = choice_connections_by_od(choice_result);
        const auto demand_by_od  = demand_entries_by_od(input);
        const auto shares_by_key = shares_by_demand_key(split_result);

        std::map<OdKey, bool> all_ods;
        for (const auto& [od, _] : search_counts) {
            all_ods.emplace(od, true);
        }
        for (const auto& [od, _] : chosen_by_od) {
            all_ods.emplace(od, true);
        }
        for (const auto& [od, _] : demand_by_od) {
            all_ods.emplace(od, true);
        }

        AssignmentOutput output;
        output.summary.search_connection_count = search_result.connections.size();
        output.summary.chosen_connection_count = choice_result.connections.size();
        output.summary.demand_share_count      = split_result.shares.size();

        for (const auto& demand : input.demand) {
            output.summary.total_demand_passengers += demand.passengers;
        }
        for (const auto& share : split_result.shares) {
            output.summary.assigned_passengers += share.passengers;
        }

        output.od_results.reserve(all_ods.size());

        for (const auto& [od, _] : all_ods) {
            AssignmentOdResult od_result{
                .origin                    = od.origin
                , .destination             = od.destination
                , .search_connection_count = 0
                , .chosen_connection_count = 0
                , .total_demand_passengers = 0.0
                , .assigned_passengers     = 0.0
                , .connections             = {}
                , .intervals               = {}
            };

            if (const auto search_it = search_counts.find(od); search_it != search_counts.end()) {
                od_result.search_connection_count = search_it->second;
            }

            std::map<ConnectionTraceKey, std::size_t> chosen_connection_indices;
            if (const auto chosen_it = chosen_by_od.find(od); chosen_it != chosen_by_od.end()) {
                od_result.connections.reserve(chosen_it->second.size());
                for (const auto* connection : chosen_it->second) {
                    MATHFP_TRY_LET(
                        AssignmentConnection
                        , mapped_connection
                        , build_assignment_connection(*connection, network)
                    );
                    const auto index = od_result.connections.size();
                    chosen_connection_indices.emplace(connection_trace_key(*connection), index);
                    od_result.connections.push_back(std::move(mapped_connection));
                }
                od_result.chosen_connection_count = od_result.connections.size();
            }

            if (const auto demand_it = demand_by_od.find(od); demand_it != demand_by_od.end()) {
                od_result.intervals.reserve(demand_it->second.size());
                for (const auto* demand : demand_it->second) {
                    MATHFP_TRY_LET(
                        const TimeInterval*
                        , interval
                        , find_interval(input, demand->interval)
                    );

                    AssignmentDemandInterval interval_result{
                        .interval              = *interval
                        , .demand_passengers   = demand->passengers
                        , .assigned_passengers = 0.0
                        , .shares              = {}
                    };

                    od_result.total_demand_passengers += demand->passengers;
                    const auto shares_it = shares_by_key.find(demand_key(*demand));
                    if (shares_it != shares_by_key.end()) {
                        interval_result.shares.reserve(shares_it->second.size());
                        for (const auto* share : shares_it->second) {
                            const auto trace_key     = connection_trace_key(share->connection);
                            const auto connection_it = chosen_connection_indices.find(trace_key);
                            if (connection_it == chosen_connection_indices.end()) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("assignment output mapping could not match split share to chosen connection")
                                        .ctx("origin"     , share->origin.get())
                                        .ctx("destination", share->destination.get())
                                        .ctx("interval_id", share->interval.get())
                                );
                            }

                            interval_result.shares.push_back(
                                AssignmentIntervalShare{
                                    .connection_index = AssignmentConnectionRef{
                                        static_cast<std::int64_t>(connection_it->second)
                                    }
                                    , .passengers      = share->passengers
                                    , .probability     = share->probability
                                    , .independence    = share->independence
                                    , .split_impedance = share->split_impedance
                                }
                            );
                            interval_result.assigned_passengers += share->passengers;
                        }
                    }

                    od_result.assigned_passengers += interval_result.assigned_passengers;
                    od_result.intervals.push_back(std::move(interval_result));
                }
            }

            output.od_results.push_back(std::move(od_result));
        }

        output.summary.od_count = output.od_results.size();
        MATHFP_TRY(validate_output_summary_semantics(output));
        return output;
    }

}  // namespace timetable::domain::assignment
