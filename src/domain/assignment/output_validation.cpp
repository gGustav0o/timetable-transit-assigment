#include "detail/output_internal.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
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

        struct StopFlowAggregate final {
            mathfp::CompensatedSum<double> incoming_passenger_segments{};
            mathfp::CompensatedSum<double> outgoing_passenger_segments{};
        };

        using LineLoadKey = std::tuple<std::int64_t, std::int64_t>;
        using RouteLoadKey = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
        using TripLoadKey = std::tuple<std::int64_t, std::int64_t, std::int64_t, std::int64_t>;
        using StopLoadKey = std::tuple<std::int64_t, std::int64_t>;

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

        [[nodiscard]] StopLoadKey from_stop_load_key(
            const AssignmentSegmentLoad& load
        ) noexcept {
            return StopLoadKey{ load.interval.get(), load.from.stop.get() };
        }

        [[nodiscard]] StopLoadKey to_stop_load_key(
            const AssignmentSegmentLoad& load
        ) noexcept {
            return StopLoadKey{ load.interval.get(), load.to.stop.get() };
        }

        [[nodiscard]] StopLoadKey stop_load_key(
            const AssignmentStopLoad& load
        ) noexcept {
            return StopLoadKey{ load.interval.get(), load.stop.get() };
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

        [[nodiscard]] bool valid_stop_load(
            const AssignmentStopLoad& load
        ) noexcept {
            return std::isfinite(load.boarding_passengers)
                && std::isfinite(load.alighting_passengers)
                && std::isfinite(load.transfer_boarding_passengers)
                && std::isfinite(load.transfer_alighting_passengers)
                && std::isfinite(load.incoming_passenger_segments)
                && std::isfinite(load.outgoing_passenger_segments)
                && std::isfinite(load.through_passengers)
                && load.boarding_passengers >= 0.0
                && load.alighting_passengers >= 0.0
                && load.transfer_boarding_passengers >= 0.0
                && load.transfer_alighting_passengers >= 0.0
                && load.incoming_passenger_segments >= 0.0
                && load.outgoing_passenger_segments >= 0.0
                && load.through_passengers >= 0.0
                && load.transfer_boarding_passengers <= load.boarding_passengers
                && load.transfer_alighting_passengers <= load.alighting_passengers;
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

        [[nodiscard]] std::map<StopLoadKey, StopFlowAggregate> aggregate_stop_flows(
            const std::vector<AssignmentSegmentLoad>& segment_loads
        ) {
            std::map<StopLoadKey, StopFlowAggregate> aggregates;
            for (const auto& load : segment_loads) {
                aggregates[from_stop_load_key(load)]
                    .outgoing_passenger_segments.add(load.passengers);
                aggregates[to_stop_load_key(load)]
                    .incoming_passenger_segments.add(load.passengers);
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

            const auto stop_flow_aggregates = aggregate_stop_flows(loads.segment_loads);
            if (loads.stop_loads.size() != stop_flow_aggregates.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output stop load count disagrees with segment-load stop aggregates")
                        .ctx("declared_stop_loads", static_cast<std::int64_t>(loads.stop_loads.size()))
                        .ctx("actual_stop_loads"  , static_cast<std::int64_t>(stop_flow_aggregates.size()))
                );
            }

            for (std::size_t i = 0; i < loads.stop_loads.size(); ++i) {
                const auto& load = loads.stop_loads[i];
                const auto aggregate_it = stop_flow_aggregates.find(stop_load_key(load));
                if (
                       aggregate_it == stop_flow_aggregates.end()
                    || !valid_stop_load(load)
                    || !almost_equal_scalar(
                          load.incoming_passenger_segments
                        , aggregate_it->second.incoming_passenger_segments.value()
                    )
                    || !almost_equal_scalar(
                          load.outgoing_passenger_segments
                        , aggregate_it->second.outgoing_passenger_segments.value()
                    )
                ) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output stop load disagrees with segment-load stop aggregate")
                            .ctx("load_index", static_cast<std::int64_t>(i))
                            .ctx("interval_id", load.interval.get())
                            .ctx("stop_id", load.stop.get())
                            .ctx("incoming_passenger_segments", load.incoming_passenger_segments)
                            .ctx("outgoing_passenger_segments", load.outgoing_passenger_segments)
                    );
                }
            }

            return mathfp::kUnit;
        }

        using SkimEntryKey = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
        using OutputIntervalLookup = std::map<SkimEntryKey, const AssignmentDemandInterval*>;

        [[nodiscard]] SkimEntryKey skim_entry_key(
            const AssignmentSkimEntry& entry
        ) noexcept {
            return SkimEntryKey{
                  entry.origin.get()
                , entry.destination.get()
                , entry.interval.get()
            };
        }

        [[nodiscard]] SkimEntryKey skim_entry_key(
              const AssignmentOdResult&        od_result
            , const AssignmentDemandInterval&  interval
        ) noexcept {
            return SkimEntryKey{
                  od_result.origin.get()
                , od_result.destination.get()
                , interval.interval.id.get()
            };
        }

        mathfp::Expected<OutputIntervalLookup> build_output_interval_lookup(
            const AssignmentOutput& output
        ) {
            OutputIntervalLookup lookup;
            for (const auto& od_result : output.od_results) {
                for (const auto& interval : od_result.intervals) {
                    const auto key = skim_entry_key(od_result, interval);
                    if (!lookup.emplace(key, &interval).second) {
                        return mathfp::unexpected(
                            mathfp::internal_error("assignment output contains duplicate OD-interval result")
                                .ctx("origin"     , od_result.origin.get())
                                .ctx("destination", od_result.destination.get())
                                .ctx("interval_id", interval.interval.id.get())
                        );
                    }
                }
            }
            return lookup;
        }

        mathfp::Expected<mathfp::Unit> validate_skim_matrix_matches_output(
            const AssignmentOutput& output
        ) {
            MATHFP_TRY(validate_assignment_skim_matrix(output.skim_matrix));
            if (output.skim_matrix.entries.empty()) {
                return mathfp::kUnit;
            }

            MATHFP_TRY_LET(OutputIntervalLookup, intervals, build_output_interval_lookup(output));
            if (output.skim_matrix.entries.size() != intervals.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output skim matrix row count disagrees with OD-interval results")
                        .ctx("skim_entries"       , static_cast<std::int64_t>(output.skim_matrix.entries.size()))
                        .ctx("output_od_intervals", static_cast<std::int64_t>(intervals.size()))
                );
            }

            for (std::size_t i = 0; i < output.skim_matrix.entries.size(); ++i) {
                const auto& entry = output.skim_matrix.entries[i];
                const auto interval_it = intervals.find(skim_entry_key(entry));
                if (interval_it == intervals.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output skim entry has no matching OD-interval result")
                            .ctx("skim_index" , static_cast<std::int64_t>(i))
                            .ctx("origin"     , entry.origin.get())
                            .ctx("destination", entry.destination.get())
                            .ctx("interval_id", entry.interval.get())
                    );
                }

                const auto& interval = *interval_it->second;
                if (!almost_equal_scalar(entry.demand_passengers, interval.demand_passengers)
                    || !almost_equal_scalar(entry.assigned_passengers, interval.assigned_passengers)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output skim entry disagrees with OD-interval demand totals")
                            .ctx("skim_index"              , static_cast<std::int64_t>(i))
                            .ctx("origin"                  , entry.origin.get())
                            .ctx("destination"             , entry.destination.get())
                            .ctx("interval_id"             , entry.interval.get())
                            .ctx("skim_demand_passengers"  , entry.demand_passengers)
                            .ctx("output_demand_passengers", interval.demand_passengers)
                            .ctx("skim_assigned_passengers", entry.assigned_passengers)
                            .ctx("output_assigned_passengers", interval.assigned_passengers)
                    );
                }
            }

            return mathfp::kUnit;
        }

        using ElementaryOverloadLookup =
            std::map<ElementarySegmentLoadKey, const ElementarySegmentOverload*>;

        [[nodiscard]] ElementaryOverloadLookup build_elementary_overload_lookup(
            const ElementarySegmentOverloadAssessment& assessment
        ) {
            ElementaryOverloadLookup lookup;
            for (const auto& overload : assessment.items) {
                lookup.emplace(overload.key, &overload);
            }
            return lookup;
        }

        mathfp::Expected<mathfp::Unit> validate_calculated_overload_uses_elementary_loads(
            const AssignmentOutput& output
        ) {
            MATHFP_TRY(validate_vehicle_journey_item_loads(output.elementary_segment_loads));

            if (output.mode != AssignmentOutputMode::Calculated) {
                if (!output.elementary_segment_loads.items.empty()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("non-assignment output must not contain elementary segment loads")
                            .ctx(
                                  "elementary_load_count"
                                , static_cast<std::int64_t>(
                                      output.elementary_segment_loads.items.size()
                                  )
                              )
                    );
                }
                return mathfp::kUnit;
            }

            if (output.vehicle_journey_item_loads.status
                != VehicleJourneyItemOverloadAssessmentStatus::Calculated) {
                return mathfp::kUnit;
            }

            const auto overloads = build_elementary_overload_lookup(
                output.vehicle_journey_item_loads
            );
            for (std::size_t i = 0; i < output.elementary_segment_loads.items.size(); ++i) {
                const auto& load = output.elementary_segment_loads.items[i];
                const auto overload_it = overloads.find(load.key);
                if (overload_it == overloads.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("calculated overload assessment misses an elementary segment load")
                            .ctx("load_index" , static_cast<std::int64_t>(i))
                            .ctx("interval_id", load.key.interval.get())
                            .ctx("trip_id"    , load.key.item.trip.get())
                            .ctx("from_index" , load.key.item.from_index.get())
                    );
                }
                if (!almost_equal_scalar(overload_it->second->passengers, load.passengers)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("calculated overload assessment passenger value disagrees with elementary segment load")
                            .ctx("load_index"          , static_cast<std::int64_t>(i))
                            .ctx("interval_id"         , load.key.interval.get())
                            .ctx("trip_id"             , load.key.item.trip.get())
                            .ctx("from_index"          , load.key.item.from_index.get())
                            .ctx("elementary_passengers", load.passengers)
                            .ctx("overload_passengers" , overload_it->second->passengers)
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
        if (od_result.diagnostics.search.alternative_count
                != od_result.search_connection_count
            || od_result.diagnostics.choice.chosen_alternative_count
                != od_result.chosen_connection_count) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output OD diagnostics disagree with compatibility counters")
                    .ctx("origin"                 , od_result.origin.get())
                    .ctx("destination"            , od_result.destination.get())
                    .ctx("search_connection_count", static_cast<std::int64_t>(od_result.search_connection_count))
                    .ctx(
                          "diagnostic_search_alternatives"
                        , static_cast<std::int64_t>(
                              od_result.diagnostics.search.alternative_count
                          )
                      )
                    .ctx("chosen_connection_count", static_cast<std::int64_t>(od_result.chosen_connection_count))
                    .ctx(
                          "diagnostic_chosen_alternatives"
                        , static_cast<std::int64_t>(
                              od_result.diagnostics.choice.chosen_alternative_count
                          )
                      )
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
        switch (output.mode) {
        case AssignmentOutputMode::Calculated:
        case AssignmentOutputMode::AllZoneSearch:
        case AssignmentOutputMode::AssignmentDisabled:
            break;
        default:
            return mathfp::unexpected(
                mathfp::internal_error("assignment output mode is unsupported")
            );
        }

        if (output.mode == AssignmentOutputMode::AssignmentDisabled
            && output.skim_matrix.status == AssignmentSkimMatrixStatus::Calculated) {
            return mathfp::unexpected(
                mathfp::internal_error("disabled assignment output cannot contain calculated skim matrix")
            );
        }
        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(output.capacity_aware));
        if (output.mode == AssignmentOutputMode::AssignmentDisabled
            && output.capacity_aware.capacity_aware_enabled) {
            return mathfp::unexpected(
                mathfp::internal_error("disabled assignment output cannot contain enabled capacity-aware diagnostics")
            );
        }
        if (output.mode == AssignmentOutputMode::Calculated
            && output.skim_matrix.status == AssignmentSkimMatrixStatus::SkippedAssignmentDisabled) {
            return mathfp::unexpected(
                mathfp::internal_error("calculated assignment output cannot mark skim as skipped by disabled assignment")
            );
        }
        if (output.mode == AssignmentOutputMode::Calculated
            && output.vehicle_journey_item_loads.status
                == VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled) {
            return mathfp::unexpected(
                mathfp::internal_error("calculated assignment output cannot mark vehicle journey item loads as skipped by disabled assignment")
            );
        }
        if (output.mode == AssignmentOutputMode::AllZoneSearch) {
            if (output.summary.chosen_connection_count != 0
                || output.summary.demand_share_count != 0
                || !almost_equal_scalar(output.summary.total_demand_passengers, 0.0)
                || !almost_equal_scalar(output.summary.assigned_passengers, 0.0)) {
                return mathfp::unexpected(
                    mathfp::internal_error("all-zone search output summary must not contain assignment quantities")
                        .ctx("chosen_connection_count", static_cast<std::int64_t>(output.summary.chosen_connection_count))
                        .ctx("demand_share_count"     , static_cast<std::int64_t>(output.summary.demand_share_count))
                        .ctx("total_demand_passengers", output.summary.total_demand_passengers)
                        .ctx("assigned_passengers"    , output.summary.assigned_passengers)
                );
            }
            if (!output.loads.line_loads.empty()
                || !output.loads.route_loads.empty()
                || !output.loads.route_total_loads.empty()
                || !output.loads.trip_loads.empty()
                || !output.loads.segment_loads.empty()
                || !output.loads.stop_loads.empty()
                || !output.loads.stop_total_loads.empty()
                || !output.elementary_segment_loads.items.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("all-zone search output must not contain load rows")
                );
            }
            if (output.skim_matrix.status == AssignmentSkimMatrixStatus::Calculated) {
                return mathfp::unexpected(
                    mathfp::internal_error("all-zone search output cannot contain calculated skim matrix")
                );
            }
            if (output.vehicle_journey_item_loads.status
                    != VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled
                || !output.vehicle_journey_item_loads.items.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("all-zone search output must not contain vehicle journey item load rows")
                        .ctx(
                              "vehicle_journey_item_load_status"
                            , std::string(to_string(output.vehicle_journey_item_loads.status))
                        )
                        .ctx(
                              "vehicle_journey_item_load_count"
                            , static_cast<std::int64_t>(output.vehicle_journey_item_loads.items.size())
                        )
                );
            }
            for (const auto& od_result : output.od_results) {
                if (od_result.chosen_connection_count != 0
                    || !almost_equal_scalar(od_result.total_demand_passengers, 0.0)
                    || !almost_equal_scalar(od_result.assigned_passengers, 0.0)
                    || !od_result.connections.empty()
                    || !od_result.intervals.empty()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("all-zone search OD result must contain only search counters")
                            .ctx("origin"                 , od_result.origin.get())
                            .ctx("destination"            , od_result.destination.get())
                            .ctx("chosen_connection_count", static_cast<std::int64_t>(od_result.chosen_connection_count))
                            .ctx("total_demand_passengers", od_result.total_demand_passengers)
                            .ctx("assigned_passengers"    , od_result.assigned_passengers)
                            .ctx("connection_count"       , static_cast<std::int64_t>(od_result.connections.size()))
                            .ctx("interval_count"         , static_cast<std::int64_t>(od_result.intervals.size()))
                    );
                }
            }
        }
        if (output.mode == AssignmentOutputMode::AssignmentDisabled) {
            if (output.summary.search_connection_count != 0
                || output.summary.chosen_connection_count != 0
                || output.summary.demand_share_count != 0
                || !almost_equal_scalar(output.summary.assigned_passengers, 0.0)) {
                return mathfp::unexpected(
                    mathfp::internal_error("disabled assignment output summary must not contain calculated assignment quantities")
                        .ctx("search_connection_count", static_cast<std::int64_t>(output.summary.search_connection_count))
                        .ctx("chosen_connection_count", static_cast<std::int64_t>(output.summary.chosen_connection_count))
                        .ctx("demand_share_count"     , static_cast<std::int64_t>(output.summary.demand_share_count))
                        .ctx("assigned_passengers"    , output.summary.assigned_passengers)
                );
            }

            if (!output.loads.line_loads.empty()
                || !output.loads.route_loads.empty()
                || !output.loads.route_total_loads.empty()
                || !output.loads.trip_loads.empty()
                || !output.loads.segment_loads.empty()
                || !output.loads.stop_loads.empty()
                || !output.loads.stop_total_loads.empty()
                || !output.elementary_segment_loads.items.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("disabled assignment output must not contain load rows")
                );
            }
            if (output.vehicle_journey_item_loads.status
                    != VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled
                || !output.vehicle_journey_item_loads.items.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("disabled assignment output must not contain vehicle journey item load rows")
                        .ctx(
                              "vehicle_journey_item_load_status"
                            , std::string(to_string(output.vehicle_journey_item_loads.status))
                        )
                        .ctx(
                              "vehicle_journey_item_load_count"
                            , static_cast<std::int64_t>(output.vehicle_journey_item_loads.items.size())
                        )
                );
            }

            for (const auto& od_result : output.od_results) {
                if (od_result.search_connection_count != 0
                    || od_result.chosen_connection_count != 0
                    || !almost_equal_scalar(od_result.assigned_passengers, 0.0)
                    || !od_result.connections.empty()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("disabled assignment OD result must not contain calculated connections")
                            .ctx("origin"                 , od_result.origin.get())
                            .ctx("destination"            , od_result.destination.get())
                            .ctx("search_connection_count", static_cast<std::int64_t>(od_result.search_connection_count))
                            .ctx("chosen_connection_count", static_cast<std::int64_t>(od_result.chosen_connection_count))
                            .ctx("assigned_passengers"    , od_result.assigned_passengers)
                            .ctx("connection_count"       , static_cast<std::int64_t>(od_result.connections.size()))
                    );
                }
                for (const auto& interval : od_result.intervals) {
                    if (!almost_equal_scalar(interval.assigned_passengers, 0.0)
                        || !interval.shares.empty()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("disabled assignment interval must not contain split shares")
                                .ctx("origin"             , od_result.origin.get())
                                .ctx("destination"        , od_result.destination.get())
                                .ctx("interval_id"        , interval.interval.id.get())
                                .ctx("assigned_passengers", interval.assigned_passengers)
                                .ctx("share_count"        , static_cast<std::int64_t>(interval.shares.size()))
                        );
                    }
                }
            }
        }

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
        MATHFP_TRY(validate_calculated_overload_uses_elementary_loads(output));
        MATHFP_TRY(validate_vehicle_journey_item_overload_assessment(
            output.vehicle_journey_item_loads
        ));
        MATHFP_TRY(validate_skim_matrix_matches_output(output));

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

        if (output.summary.diagnostics.search_alternative_count
                != output.summary.search_connection_count
            || output.summary.diagnostics.chosen_alternative_count
                != output.summary.chosen_connection_count) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output summary diagnostics disagree with compatibility counters")
                    .ctx("search_connection_count", static_cast<std::int64_t>(output.summary.search_connection_count))
                    .ctx(
                          "diagnostic_search_alternatives"
                        , static_cast<std::int64_t>(
                              output.summary.diagnostics.search_alternative_count
                          )
                      )
                    .ctx("chosen_connection_count", static_cast<std::int64_t>(output.summary.chosen_connection_count))
                    .ctx(
                          "diagnostic_chosen_alternatives"
                        , static_cast<std::int64_t>(
                              output.summary.diagnostics.chosen_alternative_count
                          )
                      )
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
