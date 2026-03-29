#include "timetable/domain/assignment/validation.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "../detail/validation_common.hpp"
#include "timetable/domain/impedance.hpp"
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
            TransferCount transfers{};
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

        [[nodiscard]] double expected_search_impedance(
              Time                journey_time
            , TransferCount       transfers
            , double              fare
            , double              fare_scale
            , const SearchParams& params
        ) noexcept {
            return connection_impedance_value(
                  journey_time
                , transfers
                , fare
                , params.impedance
                , fare_scale
            );
        }

        mathfp::Expected<mathfp::Unit> validate_discovered_connection_basic(
              const DiscoveredConnection& connection
            , std::size_t                 index
        ) {
            if (connection.origin == connection.destination) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection origin and destination must be distinct")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("origin"          , connection.origin.get())
                        .ctx("destination"     , connection.destination.get())
                );
            }
            if (connection.segments.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection must contain at least one segment")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (   !detail::validation::is_finite_non_negative(connection.departure    .value())
                || !detail::validation::is_finite_non_negative(connection.arrival      .value())
                || !detail::validation::is_finite_non_negative(connection.journey_time .value())
                || !detail::validation::is_finite_non_negative(connection.transfer_time.value())
                || !detail::validation::is_finite_non_negative(connection.fare)
                || !std::isfinite(connection.impedance)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection carries non-finite metric")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (connection.arrival.value() < connection.departure.value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection arrival precedes departure")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("departure"       , connection.departure.value())
                        .ctx("arrival"         , connection.arrival.value())
                );
            }
            if (!detail::validation::almost_equal_scalar(
                  connection.journey_time.value()
                , connection.arrival.value() - connection.departure.value()
            )) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection journey_time is inconsistent with departure/arrival")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (connection.transfer_time.value() > connection.journey_time.value()
                && !detail::validation::almost_equal_scalar(
                      connection.transfer_time.value()
                    , connection.journey_time.value()
                )) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection transfer_time exceeds journey_time")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (connection.transfers.get() < 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection transfer count must be non-negative")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("transfers"       , static_cast<std::int64_t>(connection.transfers.get()))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<EvaluatedConnectionTrace> evaluate_connection_trace(
              const DiscoveredConnection& connection
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

            for (std::size_t i = 0; i < connection.segments.size(); ++i) {
                const auto segment_id    = connection.segments[i];
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
                    transfers = TransferCount{ transfers.get() + 1 };
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

        mathfp::Expected<mathfp::Unit> validate_evaluated_connection_against_declared(
              const DiscoveredConnection&     connection
            , const EvaluatedConnectionTrace& evaluated
            , double                          fare_scale
            , const SearchParams&             params
            , std::size_t                     index
        ) {
            if (evaluated.start.kind != EndpointKind::Zone || evaluated.start.id != connection.origin.get()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not start at declared origin zone")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("origin"          , connection.origin.get())
                );
            }
            if (evaluated.finish.kind != EndpointKind::Zone || evaluated.finish.id != connection.destination.get()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not end at declared destination zone")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("destination"     , connection.destination.get())
                );
            }
            if (!detail::validation::almost_equal_time(connection.departure, evaluated.departure)
                || !detail::validation::almost_equal_time(connection.arrival, evaluated.arrival)
                || !detail::validation::almost_equal_time(connection.journey_time, evaluated.journey_time)
                || !detail::validation::almost_equal_time(connection.transfer_time, evaluated.transfer_time)
                || connection.transfers != evaluated.transfers
                || !detail::validation::almost_equal_scalar(connection.fare, evaluated.fare)) {
                return mathfp::unexpected(
                    mathfp::internal_error("declared connection metrics disagree with segment trace")
                        .ctx("connection_index"   , static_cast<std::int64_t>(index))
                        .ctx("declared_departure" , connection.departure.value())
                        .ctx("evaluated_departure", evaluated.departure.value())
                        .ctx("declared_arrival"   , connection.arrival.value())
                        .ctx("evaluated_arrival"  , evaluated.arrival.value())
                );
            }

            const auto impedance = expected_search_impedance(
                  evaluated.journey_time
                , evaluated.transfers
                , evaluated.fare
                , fare_scale
                , params
            );
            if (!detail::validation::almost_equal_scalar(connection.impedance, impedance)) {
                return mathfp::unexpected(
                    mathfp::internal_error("declared connection impedance disagrees with search formula")
                        .ctx("connection_index"   , static_cast<std::int64_t>(index))
                        .ctx("declared_impedance" , connection.impedance)
                        .ctx("evaluated_impedance", impedance)
                );
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_step_output(
          const ConnectionSearchResult& result
        , const PreprocessedNetwork&    network
        , double                        fare_scale
        , const SearchParams&           params
    ) {
        if (!(fare_scale > 0.0) || !std::isfinite(fare_scale)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search fare_scale must be finite and strictly positive")
                    .ctx("fare_scale", fare_scale)
            );
        }

        if (result.connections.empty()) {
            detail::validation::warn("search output: no feasible connections were found");
            return mathfp::kUnit;
        }

        MATHFP_TRY(detail::validation::validate_unique_connection_traces(result.connections, "search"));
        for (std::size_t i = 0; i < result.connections.size(); ++i) {
            const auto& connection = result.connections[i];
            MATHFP_TRY(validate_discovered_connection_basic(connection, i));
            MATHFP_TRY_LET(
                  EvaluatedConnectionTrace
                , evaluated
                , evaluate_connection_trace(connection, network, i)
            );
            MATHFP_TRY(validate_evaluated_connection_against_declared(
                  connection
                , evaluated
                , fare_scale
                , params
                , i
            ));
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
