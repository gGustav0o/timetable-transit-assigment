#include "detail/output_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment::detail {

    namespace {

        bool almost_equal_scalar(
              double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

        struct LoadAggregate final {
            mathfp::CompensatedSum<double> passenger_segments{};
            std::size_t segment_load_count{};
        };

        using LineLoadKey = std::tuple<std::int64_t, std::int64_t>;
        using RouteLoadKey = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
        using TripLoadKey = std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t>;

        [[nodiscard]] LineLoadKey line_load_key(
            const AssignmentSegmentLoad& load
        ) noexcept {
            return LineLoadKey{ load.interval.get(), load.line.get() };
        }

        [[nodiscard]] LineLoadKey line_load_key(
            const AssignmentLineLoad& load
        ) noexcept {
            return LineLoadKey{ load.interval.get(), load.line.get() };
        }

        [[nodiscard]] TripLoadKey trip_load_key(
            const AssignmentSegmentLoad& load
        ) noexcept {
            return TripLoadKey{ load.interval.get(), load.line.get(), load.route.get(), load.trip.get() };
        }

        [[nodiscard]] TripLoadKey trip_load_key(
            const AssignmentTripLoad& load
        ) noexcept {
            return TripLoadKey{ load.interval.get(), load.line.get(), load.route.get(), load.trip.get() };
        }

        [[nodiscard]] RouteLoadKey route_load_key(
            const AssignmentSegmentLoad& load
        ) noexcept {
            return RouteLoadKey{ load.interval.get(), load.line.get(), load.route.get() };
        }

        [[nodiscard]] RouteLoadKey route_load_key(
            const AssignmentRouteLoad& load
        ) noexcept {
            return RouteLoadKey{ load.interval.get(), load.line.get(), load.route.get() };
        }

        [[nodiscard]] bool valid_segment_load(
            const AssignmentSegmentLoad& load
        ) noexcept {
            return load.passengers >= 0.0
                && std::isfinite(load.passengers)
                && load.departure.value() <= load.arrival.value();
        }

        [[nodiscard]] bool valid_aggregate_load(
              double      passenger_segments
            , std::size_t segment_load_count
        ) noexcept {
            return passenger_segments >= 0.0
                && std::isfinite(passenger_segments)
                && segment_load_count > 0;
        }

        [[nodiscard]] std::map<LineLoadKey, LoadAggregate> aggregate_line_loads(
            const std::vector<AssignmentSegmentLoad>& segment_loads
        ) {
            std::map<LineLoadKey, LoadAggregate> aggregates;
            for (const auto& load : segment_loads) {
                auto& aggregate = aggregates[line_load_key(load)];
                aggregate.passenger_segments.add(load.passengers);
                aggregate.segment_load_count += 1;
            }
            return aggregates;
        }

        [[nodiscard]] std::map<TripLoadKey, LoadAggregate> aggregate_trip_loads(
            const std::vector<AssignmentSegmentLoad>& segment_loads
        ) {
            std::map<TripLoadKey, LoadAggregate> aggregates;
            for (const auto& load : segment_loads) {
                auto& aggregate = aggregates[trip_load_key(load)];
                aggregate.passenger_segments.add(load.passengers);
                aggregate.segment_load_count += 1;
            }
            return aggregates;
        }

        [[nodiscard]] std::map<RouteLoadKey, LoadAggregate> aggregate_route_loads(
            const std::vector<AssignmentSegmentLoad>& segment_loads
        ) {
            std::map<RouteLoadKey, LoadAggregate> aggregates;
            for (const auto& load : segment_loads) {
                auto& aggregate = aggregates[route_load_key(load)];
                aggregate.passenger_segments.add(load.passengers);
                aggregate.segment_load_count += 1;
            }
            return aggregates;
        }

        mathfp::Expected<mathfp::Unit> validate_loads_semantics(
            const AssignmentLoads& loads
        ) {
            for (std::size_t i = 0; i < loads.segment_loads.size(); ++i) {
                const auto& load = loads.segment_loads[i];
                if (!valid_segment_load(load)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output contains invalid segment load")
                            .ctx("load_index"           , static_cast<std::int64_t>(i))
                            .ctx("interval_id"          , load.interval.get())
                            .ctx("line_id"              , load.line.get())
                            .ctx("route_id"             , load.route.get())
                            .ctx("trip_id"              , load.trip.get())
                            .ctx("route_segment_id"     , load.route_segment.get())
                            .ctx("connection_segment_id", load.connection_segment.get())
                            .ctx("passengers"           , load.passengers)
                    );
                }
            }

            const auto line_aggregates = aggregate_line_loads(loads.segment_loads);
            if (loads.line_loads.size() != line_aggregates.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output line load count disagrees with segment-load aggregates")
                        .ctx("declared_line_loads", static_cast<std::int64_t>(loads.line_loads.size()))
                        .ctx("actual_line_loads"  , static_cast<std::int64_t>(line_aggregates.size()))
                );
            }

            for (std::size_t i = 0; i < loads.line_loads.size(); ++i) {
                const auto& load = loads.line_loads[i];
                const auto aggregate_it = line_aggregates.find(line_load_key(load));
                if (
                       aggregate_it == line_aggregates.end()
                    || !valid_aggregate_load(load.passenger_segments, load.segment_load_count)
                    || !almost_equal_scalar(load.passenger_segments, aggregate_it->second.passenger_segments.value())
                    || load.segment_load_count != aggregate_it->second.segment_load_count
                ) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output line load disagrees with segment-load aggregate")
                            .ctx("load_index"       , static_cast<std::int64_t>(i))
                            .ctx("interval_id"      , load.interval.get())
                            .ctx("line_id"          , load.line.get())
                            .ctx("passenger_segments", load.passenger_segments)
                    );
                }
            }

            const auto route_aggregates = aggregate_route_loads(loads.segment_loads);
            if (loads.route_loads.size() != route_aggregates.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output route load count disagrees with segment-load aggregates")
                        .ctx("declared_route_loads", static_cast<std::int64_t>(loads.route_loads.size()))
                        .ctx("actual_route_loads"  , static_cast<std::int64_t>(route_aggregates.size()))
                );
            }

            for (std::size_t i = 0; i < loads.route_loads.size(); ++i) {
                const auto& load = loads.route_loads[i];
                const auto aggregate_it = route_aggregates.find(route_load_key(load));
                if (
                       aggregate_it == route_aggregates.end()
                    || !valid_aggregate_load(load.passenger_segments, load.segment_load_count)
                    || !almost_equal_scalar(load.passenger_segments, aggregate_it->second.passenger_segments.value())
                    || load.segment_load_count != aggregate_it->second.segment_load_count
                ) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output route load disagrees with segment-load aggregate")
                            .ctx("load_index"       , static_cast<std::int64_t>(i))
                            .ctx("interval_id"      , load.interval.get())
                            .ctx("line_id"          , load.line.get())
                            .ctx("route_id"         , load.route.get())
                            .ctx("passenger_segments", load.passenger_segments)
                    );
                }
            }

            const auto trip_aggregates = aggregate_trip_loads(loads.segment_loads);
            if (loads.trip_loads.size() != trip_aggregates.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output trip load count disagrees with segment-load aggregates")
                        .ctx("declared_trip_loads", static_cast<std::int64_t>(loads.trip_loads.size()))
                        .ctx("actual_trip_loads"  , static_cast<std::int64_t>(trip_aggregates.size()))
                );
            }

            for (std::size_t i = 0; i < loads.trip_loads.size(); ++i) {
                const auto& load = loads.trip_loads[i];
                const auto aggregate_it = trip_aggregates.find(trip_load_key(load));
                if (
                       aggregate_it == trip_aggregates.end()
                    || !valid_aggregate_load(load.passenger_segments, load.segment_load_count)
                    || !almost_equal_scalar(load.passenger_segments, aggregate_it->second.passenger_segments.value())
                    || load.segment_load_count != aggregate_it->second.segment_load_count
                ) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output trip load disagrees with segment-load aggregate")
                            .ctx("load_index"       , static_cast<std::int64_t>(i))
                            .ctx("interval_id"      , load.interval.get())
                            .ctx("line_id"          , load.line.get())
                            .ctx("route_id"         , load.route.get())
                            .ctx("trip_id"          , load.trip.get())
                            .ctx("passenger_segments", load.passenger_segments)
                    );
                }
            }

            return mathfp::kUnit;
        }

    }  // namespace

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

        mathfp::CompensatedSum<double> demand_sum;
        mathfp::CompensatedSum<double> assigned_sum;
        for (const auto& interval : od_result.intervals) {
            mathfp::CompensatedSum<double> interval_assigned_sum;
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
                interval_assigned_sum.add(share.passengers);
            }

            if (!almost_equal_scalar(interval.assigned_passengers, interval_assigned_sum.value())) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output interval assigned_passengers disagrees with its shares")
                        .ctx("origin"           , od_result.origin.get())
                        .ctx("destination"      , od_result.destination.get())
                        .ctx("interval_id"      , interval.interval.id.get())
                        .ctx("declared_assigned", interval.assigned_passengers)
                        .ctx("actual_assigned"  , interval_assigned_sum.value())
                );
            }

            demand_sum.add(interval.demand_passengers);
            assigned_sum.add(interval.assigned_passengers);
        }

        if (!almost_equal_scalar(od_result.total_demand_passengers, demand_sum.value())) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output OD total_demand_passengers disagrees with interval totals")
                    .ctx("origin"               , od_result.origin.get())
                    .ctx("destination"          , od_result.destination.get())
                    .ctx("declared_total_demand", od_result.total_demand_passengers)
                    .ctx("actual_total_demand"  , demand_sum.value())
            );
        }

        if (!almost_equal_scalar(od_result.assigned_passengers, assigned_sum.value())) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output OD assigned_passengers disagrees with interval totals")
                    .ctx("origin"           , od_result.origin.get())
                    .ctx("destination"      , od_result.destination.get())
                    .ctx("declared_assigned", od_result.assigned_passengers)
                    .ctx("actual_assigned"  , assigned_sum.value())
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

        mathfp::CompensatedSum<double> total_demand;
        mathfp::CompensatedSum<double> assigned;

        for (const auto& od_result : output.od_results) {
            MATHFP_TRY(validate_od_result_semantics(od_result));
            search_count += od_result.search_connection_count;
            chosen_count += od_result.chosen_connection_count;
            total_demand.add(od_result.total_demand_passengers);
            assigned.add(od_result.assigned_passengers);
            for (const auto& interval : od_result.intervals) {
                share_count += interval.shares.size();
            }
        }

        MATHFP_TRY(validate_loads_semantics(output.loads));

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
            || !almost_equal_scalar(output.summary.total_demand_passengers, total_demand.value())
            || !almost_equal_scalar(output.summary.assigned_passengers, assigned.value())) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output summary disagrees with OD aggregates")
                    .ctx("declared_search_connections", static_cast<std::int64_t>(output.summary.search_connection_count))
                    .ctx("actual_search_connections"  , static_cast<std::int64_t>(search_count))
                    .ctx("declared_chosen_connections", static_cast<std::int64_t>(output.summary.chosen_connection_count))
                    .ctx("actual_chosen_connections"  , static_cast<std::int64_t>(chosen_count))
                    .ctx("declared_share_count"       , static_cast<std::int64_t>(output.summary.demand_share_count))
                    .ctx("actual_share_count"         , static_cast<std::int64_t>(share_count))
                    .ctx("declared_total_demand"      , output.summary.total_demand_passengers)
                    .ctx("actual_total_demand"        , total_demand.value())
                    .ctx("declared_assigned"          , output.summary.assigned_passengers)
                    .ctx("actual_assigned"            , assigned.value())
            );
        }

        if (output.summary.runtime_seconds.has_value()
            && (!std::isfinite(*output.summary.runtime_seconds)
                || *output.summary.runtime_seconds < 0.0)) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output summary runtime is invalid")
                    .ctx("runtime_seconds", *output.summary.runtime_seconds)
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::detail
