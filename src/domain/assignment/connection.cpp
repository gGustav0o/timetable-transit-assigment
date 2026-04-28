#include "timetable/domain/assignment/connection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/validation.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] Length zero_length() noexcept {
            return Length{ 0.0 };
        }

        [[nodiscard]] bool same_endpoint(
              EndpointKey lhs
            , EndpointKey rhs
        ) noexcept {
            return lhs == rhs;
        }

        [[nodiscard]] bool same_time(
              Time lhs
            , Time rhs
        ) noexcept {
            return mathfp::almost_equal(lhs.value(), rhs.value());
        }

        [[nodiscard]] bool same_length(
              Length lhs
            , Length rhs
        ) noexcept {
            return mathfp::almost_equal(lhs.value(), rhs.value());
        }

        [[nodiscard]] bool same_fare(
              double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

        [[nodiscard]] mathfp::Error trace_error(
              std::string_view message
            , std::size_t leg_index
        ) {
            return mathfp::invalid_arg(message)
                .ctx("leg_index", static_cast<std::int64_t>(leg_index));
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_present_supply_segment(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            if (leg.connection_segment.has_value() && leg.route_segment.has_value()) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                trace_error("movement leg must reference supply segments", leg_index)
                    .ctx("kind", std::string(to_string(leg.kind)))
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_absent_supply_segment(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            if (!leg.connection_segment.has_value() && !leg.route_segment.has_value()) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                trace_error("waiting leg must not reference supply segments", leg_index)
                    .ctx("kind", std::string(to_string(leg.kind)))
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_no_line_trip(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            if (!leg.line.has_value() && !leg.trip.has_value()) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                trace_error("non-ride leg must not carry line or trip", leg_index)
                    .ctx("kind", std::string(to_string(leg.kind)))
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_no_occurrences(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            if (!leg.occurrence_from.has_value() && !leg.occurrence_to.has_value()) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                trace_error("non-ride leg must not carry stop occurrences", leg_index)
                    .ctx("kind", std::string(to_string(leg.kind)))
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_ride_context(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            if (
                   leg.line.has_value()
                && leg.trip.has_value()
                && leg.occurrence_from.has_value()
                && leg.occurrence_to.has_value()
            ) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                trace_error("ride leg must carry line, trip and stop occurrences", leg_index)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_ride_occurrence_shape(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            const auto from_endpoint = endpoint_key(leg.occurrence_from->stop);
            const auto to_endpoint   = endpoint_key(leg.occurrence_to->stop);

            if (!same_endpoint(leg.physical_from, from_endpoint)) {
                return mathfp::unexpected(
                    trace_error("ride leg physical origin does not match origin occurrence", leg_index)
                );
            }
            if (!same_endpoint(leg.physical_to, to_endpoint)) {
                return mathfp::unexpected(
                    trace_error("ride leg physical destination does not match destination occurrence", leg_index)
                );
            }
            if (!(leg.occurrence_from->position < leg.occurrence_to->position)) {
                return mathfp::unexpected(
                    trace_error("ride leg destination occurrence must follow origin occurrence", leg_index)
                        .ctx("from_position", leg.occurrence_from->position.get())
                        .ctx("to_position"  , leg.occurrence_to->position.get())
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_wait_leg_shape(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            MATHFP_TRY(ensure_absent_supply_segment(leg, leg_index));
            MATHFP_TRY(ensure_no_line_trip(leg, leg_index));
            MATHFP_TRY(ensure_no_occurrences(leg, leg_index));

            if (!same_endpoint(leg.physical_from, leg.physical_to)) {
                return mathfp::unexpected(
                    trace_error("waiting leg must stay at one physical endpoint", leg_index)
                );
            }
            if (!same_length(leg.length, zero_length())) {
                return mathfp::unexpected(
                    trace_error("waiting leg must have zero length", leg_index)
                        .ctx("length", leg.length.value())
                );
            }
            if (!same_fare(leg.fare, 0.0)) {
                return mathfp::unexpected(
                    trace_error("waiting leg must have zero fare", leg_index)
                        .ctx("fare", leg.fare)
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_walk_leg_shape(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            MATHFP_TRY(ensure_present_supply_segment(leg, leg_index));
            MATHFP_TRY(ensure_no_line_trip(leg, leg_index));
            MATHFP_TRY(ensure_no_occurrences(leg, leg_index));
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_ride_leg_shape(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            MATHFP_TRY(ensure_present_supply_segment(leg, leg_index));
            MATHFP_TRY(ensure_ride_context(leg, leg_index));
            if (
                   leg.physical_from.kind != EndpointKind::Stop
                || leg.physical_to.kind   != EndpointKind::Stop
            ) {
                return mathfp::unexpected(
                    trace_error("ride leg physical endpoints must be stops", leg_index)
                );
            }
            MATHFP_TRY(ensure_ride_occurrence_shape(leg, leg_index));
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_connection_leg(
              const ConnectionLeg& leg
            , std::size_t          leg_index
        ) {
            MATHFP_TRY(timetable::domain::validation::ensure_nonneg(leg.start_time, "connection_leg.start_time"));
            MATHFP_TRY(timetable::domain::validation::ensure_nonneg(leg.end_time, "connection_leg.end_time"));
            MATHFP_TRY(timetable::domain::validation::ensure_nonneg(leg.length, "connection_leg.length"));

            if (!std::isfinite(leg.fare) || leg.fare < 0.0) {
                return mathfp::unexpected(
                    trace_error("connection leg fare must be finite and non-negative", leg_index)
                        .ctx("fare", leg.fare)
                );
            }

            if (leg.end_time.value() < leg.start_time.value()
                && !same_time(leg.start_time, leg.end_time)) {
                return mathfp::unexpected(
                    trace_error("connection leg end time precedes start time", leg_index)
                        .ctx("start_time", leg.start_time.value())
                        .ctx("end_time"  , leg.end_time.value())
                );
            }

            if (is_wait_leg(leg.kind)) {
                return ensure_wait_leg_shape(leg, leg_index);
            }
            if (is_walk_leg(leg.kind)) {
                return ensure_walk_leg_shape(leg, leg_index);
            }
            if (is_ride_leg(leg.kind)) {
                return ensure_ride_leg_shape(leg, leg_index);
            }
            return mathfp::unexpected(
                trace_error("unknown connection leg kind", leg_index)
                    .ctx("kind", static_cast<std::int64_t>(leg.kind))
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_trace_continuity(
            const ConnectionTrace& trace
        ) {
            for (std::size_t i = 1; i < trace.legs.size(); ++i) {
                const auto& previous = trace.legs[i - 1];
                const auto& current  = trace.legs[i];

                if (!same_endpoint(previous.physical_to, current.physical_from)) {
                    return mathfp::unexpected(
                        trace_error("connection trace physical endpoints are not contiguous", i)
                            .ctx("previous_kind", std::string(to_string(previous.kind)))
                            .ctx("current_kind" , std::string(to_string(current .kind)))
                    );
                }

                if (!same_time(previous.end_time, current.start_time)) {
                    return mathfp::unexpected(
                        trace_error("connection trace has implicit time gap or overlap; waiting must be explicit", i)
                            .ctx("previous_end_time", previous.end_time.value())
                            .ctx("current_start_time", current.start_time.value())
                    );
                }
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] Time leg_duration(
            const ConnectionLeg& leg
        ) noexcept {
            return Time{ leg.end_time.value() - leg.start_time.value() };
        }

        struct MetricAccumulator final {
            Time   access_time{};
            Time   initial_wait_time{};
            Time   in_vehicle_time{};
            Time   transfer_wait_time{};
            Time   transfer_walk_time{};
            Time   egress_time{};
            Time   final_wait_time{};
            Length in_vehicle_distance{};
            Length walk_distance{};
            double fare{};
            std::int32_t ride_count{};
        };

        [[nodiscard]] MetricAccumulator accumulate_leg_metrics(
              MetricAccumulator acc
            , const ConnectionLeg& leg
        ) noexcept {
            const auto duration = leg_duration(leg);
            switch (leg.kind) {
            case ConnectionLegKind::AccessWalk:
                acc.access_time   = acc.access_time + duration;
                acc.walk_distance = acc.walk_distance + leg.length;
                break;
            case ConnectionLegKind::InitialWait:
                acc.initial_wait_time = acc.initial_wait_time + duration;
                break;
            case ConnectionLegKind::Ride:
                acc.in_vehicle_time     = acc.in_vehicle_time + duration;
                acc.in_vehicle_distance = acc.in_vehicle_distance + leg.length;
                acc.fare += leg.fare;
                ++acc.ride_count;
                break;
            case ConnectionLegKind::TransferWait:
                acc.transfer_wait_time = acc.transfer_wait_time + duration;
                break;
            case ConnectionLegKind::TransferWalk:
                acc.transfer_walk_time = acc.transfer_walk_time + duration;
                acc.walk_distance      = acc.walk_distance + leg.length;
                break;
            case ConnectionLegKind::EgressWalk:
                acc.egress_time   = acc.egress_time + duration;
                acc.walk_distance = acc.walk_distance + leg.length;
                break;
            case ConnectionLegKind::FinalWait:
                acc.final_wait_time = acc.final_wait_time + duration;
                break;
            }
            return acc;
        }

        [[nodiscard]] MetricAccumulator accumulate_trace_metrics(
            const ConnectionTrace& trace
        ) noexcept {
            MetricAccumulator acc;
            for (const auto& leg : trace.legs) {
                acc = accumulate_leg_metrics(acc, leg);
            }
            return acc;
        }

        [[nodiscard]] TransferCount transfer_count_from_ride_count(
            std::int32_t ride_count
        ) noexcept {
            return TransferCount{ std::max(0, ride_count - 1) };
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_connection_endpoints(
              ZoneId                 origin
            , ZoneId                 destination
            , const ConnectionTrace& trace
        ) {
            const auto expected_origin      = endpoint_key(origin);
            const auto expected_destination = endpoint_key(destination);
            const auto& first               = trace.legs.front();
            const auto& last                = trace.legs.back();

            if (!same_endpoint(first.physical_from, expected_origin)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not start at origin zone")
                        .ctx("origin", origin.get())
                );
            }
            if (!same_endpoint(last.physical_to, expected_destination)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not end at destination zone")
                        .ctx("destination", destination.get())
                );
            }
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_connection_trace(
        const ConnectionTrace& trace
    ) {
        if (trace.legs.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("connection trace must contain at least one leg")
            );
        }

        for (std::size_t i = 0; i < trace.legs.size(); ++i) {
            MATHFP_TRY(validate_connection_leg(trace.legs[i], i));
        }
        MATHFP_TRY(validate_trace_continuity(trace));
        return mathfp::kUnit;
    }

    mathfp::Expected<ConnectionMetrics> compute_connection_metrics(
        const ConnectionTrace& trace
    ) {
        MATHFP_TRY(validate_connection_trace(trace));

        const auto& first = trace.legs.front();
        const auto& last  = trace.legs.back();
        const auto acc    = accumulate_trace_metrics(trace);

        const auto walk_time = acc.access_time + acc.transfer_walk_time + acc.egress_time;
        const auto wait_time = acc.initial_wait_time + acc.transfer_wait_time + acc.final_wait_time;
        const auto journey_time = Time{ last.end_time.value() - first.start_time.value() };

        const auto decomposed_time =
              walk_time
            + wait_time
            + acc.in_vehicle_time;

        if (!same_time(journey_time, decomposed_time)) {
            return mathfp::unexpected(
                mathfp::internal_error("connection metrics time decomposition does not match journey time")
                    .ctx("journey_time"   , journey_time.value())
                    .ctx("decomposed_time", decomposed_time.value())
            );
        }

        return ConnectionMetrics{
              .departure_time       = first.start_time
            , .arrival_time         = last.end_time
            , .journey_time         = journey_time
            , .access_time          = acc.access_time
            , .initial_wait_time    = acc.initial_wait_time
            , .in_vehicle_time      = acc.in_vehicle_time
            , .transfer_wait_time   = acc.transfer_wait_time
            , .transfer_walk_time   = acc.transfer_walk_time
            , .egress_time          = acc.egress_time
            , .final_wait_time      = acc.final_wait_time
            , .walk_time            = walk_time
            , .wait_time            = wait_time
            , .in_vehicle_distance  = acc.in_vehicle_distance
            , .walk_distance        = acc.walk_distance
            , .journey_distance     = acc.in_vehicle_distance + acc.walk_distance
            , .ride_count           = acc.ride_count
            , .transfer_count       = transfer_count_from_ride_count(acc.ride_count)
            , .fare                 = acc.fare
        };
    }

    mathfp::Expected<ConnectionMetrics> compute_connection_metrics(
        const Connection& connection
    ) {
        return compute_connection_metrics(connection.trace);
    }

    mathfp::Expected<Connection> make_connection(
          ZoneId          origin
        , ZoneId          destination
        , ConnectionTrace trace
    ) {
        MATHFP_TRY(validate_connection_trace(trace));
        MATHFP_TRY(validate_connection_endpoints(origin, destination, trace));

        return Connection{
              .origin      = origin
            , .destination = destination
            , .trace       = std::move(trace)
        };
    }

}  // namespace timetable::domain::assignment
