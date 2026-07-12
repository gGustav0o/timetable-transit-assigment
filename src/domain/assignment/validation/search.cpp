#include "timetable/domain/assignment/validation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "../detail/validation_common.hpp"
#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
    namespace {

        struct EvaluatedConnectionTrace final {
            EndpointKey   start{};
            EndpointKey   finish{};
            Time          departure{};
            Time          arrival{};
            Time          journey_time{};
            Time          transfer_time{};
            TransferCount transfers{ TransferCount{0} };
            double        fare{};
        };

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

        mathfp::Expected<mathfp::Unit> validate_search_connection_basic(
              const SearchConnection& connection
            , std::size_t                 index
        ) {
            const auto origin      = origin_of(connection);
            const auto destination = destination_of(connection);
            const auto segments    = connection_segment_trace(connection);
            const auto metrics     = metrics_of(connection);
            const auto transfer_time =
                metrics.transfer_wait_time + metrics.transfer_walk_time;

            if (origin == destination) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection origin and destination must be distinct")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("origin"          , origin.get())
                        .ctx("destination"     , destination.get())
                );
            }
            if (segments.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection must contain at least one segment")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (canonical_connection(connection).trace.legs.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection must contain a materialized leg trace")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (   !detail::validation::is_finite_non_negative(metrics.departure_time.value())
                || !detail::validation::is_finite_non_negative(metrics.arrival_time.value())
                || !detail::validation::is_finite_non_negative(metrics.journey_time.value())
                || !detail::validation::is_finite_non_negative(transfer_time.value())
                || !detail::validation::is_finite_non_negative(metrics.fare)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection carries non-finite metric")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (metrics.arrival_time.value() < metrics.departure_time.value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection arrival precedes departure")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("departure"       , metrics.departure_time.value())
                        .ctx("arrival"         , metrics.arrival_time.value())
                );
            }
            if (!detail::validation::almost_equal_scalar(
                  metrics.journey_time.value()
                , metrics.arrival_time.value() - metrics.departure_time.value()
            )) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection journey_time is inconsistent with departure/arrival")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (transfer_time.value() > metrics.journey_time.value()
                && !detail::validation::almost_equal_scalar(
                      transfer_time.value()
                    , metrics.journey_time.value()
                )) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection transfer_time exceeds journey_time")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (metrics.transfer_count.get() < 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection transfer count must be non-negative")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("transfers"       , static_cast<std::int64_t>(metrics.transfer_count.get()))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_materialized_trace_projection(
              const SearchConnection& connection
            , std::size_t                 index
        ) {
            const auto& trace = canonical_connection(connection).trace;
            const auto segments = connection_segment_trace(connection);
            MATHFP_TRY(validate_connection_trace(trace));

            std::size_t segment_index = 0;
            for (const auto& leg : trace.legs) {
                if (!leg.connection_segment.has_value()) {
                    continue;
                }
                if (segment_index >= segments.size()
                    || segments[segment_index] != *leg.connection_segment) {
                    return mathfp::unexpected(
                        mathfp::internal_error("materialized leg trace disagrees with segment trace")
                            .ctx("connection_index", static_cast<std::int64_t>(index))
                            .ctx("segment_index"   , static_cast<std::int64_t>(segment_index))
                    );
                }
                ++segment_index;
            }
            if (segment_index != segments.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("materialized leg trace misses connection segments")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("trace_segments"  , static_cast<std::int64_t>(segment_index))
                        .ctx("path_segments"   , static_cast<std::int64_t>(segments.size()))
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<EvaluatedConnectionTrace> evaluate_connection_trace(
              const SearchConnection& connection
            , const PreprocessedNetwork&  network
            , std::size_t                 index
        ) {
            std::optional<Time>        departure{};
            std::optional<Time>        current_time{};
            std::optional<EndpointKey> previous_to{};
            EndpointKey                start{};
            EndpointKey                finish{};
            Time access_walk{ 0.0 };
            Time transfer_time{ 0.0 };
            TransferCount transfers{ 0 };
            double fare            = 0.0;
            bool has_timed_segment = false;
            const auto segments = connection_segment_trace(connection);

            for (std::size_t i = 0; i < segments.size(); ++i) {
                const auto segment_id    = segments[i];
                const auto segment_index = static_cast<std::size_t>(segment_id.get());
                if (segment_index >= network.connection_segments.size()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("connection references unknown connection segment")
                            .ctx("connection_index"     , static_cast<std::int64_t>(index))
                            .ctx("segment_position"     , static_cast<std::int64_t>(i))
                            .ctx("connection_segment_id", segment_id.get())
                    );
                }

                const auto& segment       = connection_segment_at(network, segment_id);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                const auto from           = physical_from_key(route_segment);
                const auto to             = physical_to_key(route_segment);

                if (i == 0) {
                    start = from;
                } else if (*previous_to != from) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("connection segment trace is not physically contiguous")
                            .ctx("connection_index" , static_cast<std::int64_t>(index))
                            .ctx("segment_position" , static_cast<std::int64_t>(i))
                            .ctx("previous_to_kind" , static_cast<std::int64_t>(previous_to->kind))
                            .ctx("previous_to_id"   , previous_to->id)
                            .ctx("current_from_kind", static_cast<std::int64_t>(from.kind))
                            .ctx("current_from_id"  , from.id)
                    );
                }
                previous_to = to;
                finish      = to;

                if (is_walk_connection(segment)) {
                    if (!current_time.has_value()) {
                        access_walk = Time{ access_walk.value() + route_segment.run_time.value() };
                    } else {
                        current_time = Time{ current_time->value() + route_segment.run_time.value() };
                        if (to.kind == EndpointKind::Stop) {
                            transfer_time = Time{
                                transfer_time.value() + route_segment.run_time.value()
                            };
                        }
                    }
                    continue;
                }

                if (!segment.departure.has_value() || !segment.arrival.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("timed connection segment is missing schedule times")
                            .ctx("connection_index"     , static_cast<std::int64_t>(index))
                            .ctx("segment_position"     , static_cast<std::int64_t>(i))
                            .ctx("connection_segment_id", segment.id.get())
                    );
                }

                has_timed_segment = true;
                if (!departure.has_value()) {
                    departure = Time{
                        segment.departure->value() - access_walk.value()
                    };
                } else {
                    if (!current_time.has_value()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("timed continuation lost current_time state")
                                .ctx("connection_index", static_cast<std::int64_t>(index))
                                .ctx("segment_position", static_cast<std::int64_t>(i))
                        );
                    }
                    transfer_time = Time{
                        transfer_time.value() + (segment.departure->value() - current_time->value())
                    };
                    MATHFP_TRY_LET(
                          TransferCount
                        , next_transfers
                        , next_transfer_count(transfers)
                    );
                    transfers = next_transfers;
                }

                current_time = *segment.arrival;
                fare += segment.fare.value_or(0.0);
            }

            if (!has_timed_segment || !departure.has_value() || !current_time.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace must contain at least one timed segment")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }

            return EvaluatedConnectionTrace{
                  .start         = start
                , .finish        = finish
                , .departure     = *departure
                , .arrival       = *current_time
                , .journey_time  = Time{ current_time->value() - departure->value() }
                , .transfer_time = transfer_time
                , .transfers     = transfers
                , .fare          = fare
            };
        }

        mathfp::Expected<mathfp::Unit> validate_no_intermediate_zone_endpoints(
              const SearchConnection& connection
            , std::size_t             index
        ) {
            const auto& legs = canonical_connection(connection).trace.legs;
            for (std::size_t i = 0; i < legs.size(); ++i) {
                const auto& leg = legs[i];
                if (i > 0 && leg.physical_from.kind == EndpointKind::Zone) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("connection trace contains an intermediate zone origin")
                            .ctx("connection_index", static_cast<std::int64_t>(index))
                            .ctx("leg_index"       , static_cast<std::int64_t>(i))
                            .ctx("zone"            , leg.physical_from.id)
                    );
                }
                if (i + 1 < legs.size() && leg.physical_to.kind == EndpointKind::Zone) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("connection trace contains an intermediate zone destination")
                            .ctx("connection_index", static_cast<std::int64_t>(index))
                            .ctx("leg_index"       , static_cast<std::int64_t>(i))
                            .ctx("zone"            , leg.physical_to.id)
                    );
                }
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] double trace_time_scale(
              const ConnectionMetrics&      metrics
            , const EvaluatedConnectionTrace& evaluated
            , Time                         transfer_time
        ) noexcept {
            const auto event_scale = (std::max)(
                  std::abs(metrics.departure_time.value())
                , std::abs(metrics.arrival_time.value())
            );
            const auto evaluated_event_scale = (std::max)(
                  std::abs(evaluated.departure.value())
                , std::abs(evaluated.arrival.value())
            );
            const auto duration_scale = (std::max)(
                  std::abs(metrics.journey_time.value())
                , std::abs(evaluated.journey_time.value())
            );
            const auto transfer_scale = (std::max)(
                  std::abs(transfer_time.value())
                , std::abs(evaluated.transfer_time.value())
            );
            return (std::max)(
                  (std::max)(event_scale, evaluated_event_scale)
                , (std::max)(duration_scale, transfer_scale)
            );
        }

        [[nodiscard]] bool almost_equal_trace_time(
              double      lhs
            , double      rhs
            , double      time_scale
            , std::size_t term_count
        ) noexcept {
            const auto count = static_cast<double>((std::max)(std::size_t{ 1 }, term_count));
            const auto k     = (std::max)(32.0, 16.0 * count);
            return mathfp::almost_equal(
                  lhs
                , rhs
                , mathfp::abs_tolerance_scaled(time_scale, k)
                , k * mathfp::rel_tolerance_coeff<double>()
            );
        }

        mathfp::Expected<mathfp::Unit> validate_evaluated_segments_against_canonical_connection(
              const SearchConnection&     connection
            , const EvaluatedConnectionTrace& evaluated
            , std::size_t                     index
        ) {
            const auto origin      = origin_of(connection);
            const auto destination = destination_of(connection);
            const auto metrics     = metrics_of(connection);
            const auto transfer_time =
                metrics.transfer_wait_time + metrics.transfer_walk_time;
            const auto accumulation_terms = (std::max)(
                  connection_segment_trace(connection).size()
                , canonical_connection(connection).trace.legs.size()
            );
            const auto time_scale = trace_time_scale(metrics, evaluated, transfer_time);

            if (evaluated.start.kind != EndpointKind::Zone || evaluated.start.id != origin.get()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not start at canonical origin zone")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("origin"          , origin.get())
                );
            }
            if (evaluated.finish.kind != EndpointKind::Zone || evaluated.finish.id != destination.get()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not end at canonical destination zone")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("destination"     , destination.get())
                );
            }
            if (!detail::validation::almost_equal_time(metrics.departure_time, evaluated.departure)
                || !detail::validation::almost_equal_time(metrics.arrival_time, evaluated.arrival)
                || !almost_equal_trace_time(
                    metrics.journey_time.value(),
                    evaluated.journey_time.value(),
                    time_scale,
                    accumulation_terms
                )
                || !almost_equal_trace_time(
                    transfer_time.value(),
                    evaluated.transfer_time.value(),
                    time_scale,
                    accumulation_terms
                )
                || metrics.transfer_count != evaluated.transfers
                || !detail::validation::almost_equal_accumulated(
                    metrics.fare,
                    evaluated.fare,
                    accumulation_terms
                )) {
                return mathfp::unexpected(
                    mathfp::internal_error("canonical connection metrics disagree with segment trace")
                        .ctx("connection_index"   , static_cast<std::int64_t>(index))
                        .ctx("canonical_departure", metrics.departure_time.value())
                        .ctx("evaluated_departure", evaluated.departure.value())
                        .ctx("canonical_arrival"  , metrics.arrival_time.value())
                        .ctx("evaluated_arrival"  , evaluated.arrival.value())
                        .ctx("canonical_journey_time", metrics.journey_time.value())
                        .ctx("evaluated_journey_time", evaluated.journey_time.value())
                        .ctx("canonical_transfer_time", transfer_time.value())
                        .ctx("evaluated_transfer_time", evaluated.transfer_time.value())
                        .ctx("canonical_transfers", static_cast<std::int64_t>(metrics.transfer_count.get()))
                        .ctx("evaluated_transfers", static_cast<std::int64_t>(evaluated.transfers.get()))
                        .ctx("canonical_fare", metrics.fare)
                        .ctx("evaluated_fare", evaluated.fare)
                );
            }
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_step_output(
          const ConnectionSearchResult& result
        , const PreprocessedNetwork&    network
        , double                        fare_scale
        , const SearchParams&
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        if (!(fare_scale > 0.0) || !std::isfinite(fare_scale)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search fare_scale must be finite and strictly positive")
                    .ctx("fare_scale", fare_scale)
            );
        }

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));

        if (search_connection_count(result) == 0) {
            detail::validation::warn("search output: no feasible connections were found");
            return mathfp::kUnit;
        }

        for (const auto& task_result : result.task_results) {
            MATHFP_TRY(detail::validation::validate_unique_connection_traces(
                  task_result.connections
                , "search_task"
            ));

            MATHFP_TRY(detail::validation::validate_each_index(
                  task_result.connections
                , [&](const SearchConnection& connection, std::size_t i)
                    -> mathfp::Expected<mathfp::Unit> {
                    if (origin_of(connection) != task_result.task.origin
                        || destination_of(connection) != task_result.task.destination) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("search task contains connection with different OD")
                                .ctx("task_index"            , task_result.task.index.get())
                                .ctx("task_origin"           , task_result.task.origin.get())
                                .ctx("task_destination"      , task_result.task.destination.get())
                                .ctx("connection_origin"     , origin_of(connection).get())
                                .ctx("connection_destination", destination_of(connection).get())
                        );
                    }

                    if (!connection_admissible_for_demand_segment(
                          metrics_of(connection)
                        , task_result.task.interval
                        , assignment_period
                        , admissibility_config
                    )) {
                        return mathfp::unexpected(
                            mathfp::internal_error("search task contains inadmissible connection")
                                .ctx("task_index"      , task_result.task.index.get())
                                .ctx("origin"          , task_result.task.origin.get())
                                .ctx("destination"     , task_result.task.destination.get())
                                .ctx("interval_id"     , task_result.task.interval.id.get())
                                .ctx("connection_index", static_cast<std::int64_t>(i))
                        );
                    }

                    MATHFP_TRY(validate_search_connection_basic(connection, i));
                    MATHFP_TRY(validate_materialized_trace_projection(connection, i));
                    MATHFP_TRY(validate_no_intermediate_zone_endpoints(connection, i));
                    MATHFP_TRY_LET(
                          EvaluatedConnectionTrace
                        , evaluated
                        , evaluate_connection_trace(connection, network, i)
                    );
                    MATHFP_TRY(validate_evaluated_segments_against_canonical_connection(
                          connection
                        , evaluated
                        , i
                    ));
                    return mathfp::kUnit;
                }
            ));
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
