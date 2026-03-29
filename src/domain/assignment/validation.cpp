#include "timetable/domain/assignment/validation.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/infra/progress_bus.hpp"

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

        struct EvaluatedConnectionTrace final {
            EndpointKey start{};
            EndpointKey finish{};
            Time departure{};
            Time arrival{};
            Time journey_time{};
            Time transfer_time{};
            TransferCount transfers{};
            double fare{};
        };

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

        void warn(std::string_view message) {
            timetable::infra::progress::log(message, timetable::infra::LogLevel::Warning);
        }

        bool is_finite_non_negative(double value) noexcept {
            return std::isfinite(value) && value >= 0.0;
        }

        bool valid_probability(double value) noexcept {
            return std::isfinite(value) && value >= 0.0 && value <= 1.0;
        }

        bool almost_equal_time(Time lhs, Time rhs) noexcept {
            return mathfp::almost_equal(lhs.value(), rhs.value());
        }

        bool almost_equal_scalar(double lhs, double rhs) noexcept {
            return mathfp::almost_equal(lhs, rhs);
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

        OdKey od_key(const DiscoveredConnection& connection) noexcept {
            return OdKey{
                .origin = connection.origin
                , .destination = connection.destination
            };
        }

        DemandKey demand_key(const DemandEntry& demand) noexcept {
            return DemandKey{
                .origin = demand.origin
                , .destination = demand.destination
                , .interval = demand.interval
            };
        }

        double expected_search_impedance(
            Time journey_time
            , TransferCount transfers
            , double fare
            , double fare_scale
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

        mathfp::Expected<mathfp::Unit> validate_index_offsets(
            std::span<const std::size_t> offsets
            , std::size_t bucket_count
            , std::size_t order_count
            , std::string_view name
        ) {
            if (offsets.size() != bucket_count + 1) {
                return mathfp::unexpected(
                    mathfp::internal_error("index offsets size mismatch")
                        .ctx("index", std::string(name))
                        .ctx("bucket_count", static_cast<std::int64_t>(bucket_count))
                        .ctx("offset_count", static_cast<std::int64_t>(offsets.size()))
                );
            }
            if (offsets.empty()) {
                return mathfp::unexpected(
                    mathfp::internal_error("index offsets unexpectedly empty")
                        .ctx("index", std::string(name))
                );
            }
            if (offsets.front() != 0 || offsets.back() != order_count) {
                return mathfp::unexpected(
                    mathfp::internal_error("index offsets boundary mismatch")
                        .ctx("index", std::string(name))
                        .ctx("first_offset", static_cast<std::int64_t>(offsets.front()))
                        .ctx("last_offset", static_cast<std::int64_t>(offsets.back()))
                        .ctx("order_count", static_cast<std::int64_t>(order_count))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_discovered_connection_basic(
            const DiscoveredConnection& connection
            , std::size_t index
        ) {
            if (connection.origin == connection.destination) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection origin and destination must be distinct")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("origin", connection.origin.get())
                        .ctx("destination", connection.destination.get())
                );
            }
            if (connection.segments.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection must contain at least one segment")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (!is_finite_non_negative(connection.departure.value())
                || !is_finite_non_negative(connection.arrival.value())
                || !is_finite_non_negative(connection.journey_time.value())
                || !is_finite_non_negative(connection.transfer_time.value())
                || !is_finite_non_negative(connection.fare)
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
                        .ctx("departure", connection.departure.value())
                        .ctx("arrival", connection.arrival.value())
                );
            }
            if (!almost_equal_scalar(
                connection.journey_time.value()
                , connection.arrival.value() - connection.departure.value()
            )) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection journey_time is inconsistent with departure/arrival")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (connection.transfer_time.value() > connection.journey_time.value()
                && !almost_equal_scalar(connection.transfer_time.value(), connection.journey_time.value())) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection transfer_time exceeds journey_time")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                );
            }
            if (connection.transfers.get() < 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection transfer count must be non-negative")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("transfers", static_cast<std::int64_t>(connection.transfers.get()))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<EvaluatedConnectionTrace> evaluate_connection_trace(
            const DiscoveredConnection& connection
            , const PreprocessedNetwork& network
            , std::size_t index
        ) {
            std::optional<Time> departure{};
            std::optional<Time> current_time{};
            std::optional<EndpointKey> previous_to{};
            EndpointKey start{};
            EndpointKey finish{};
            Time access_walk{ 0.0 };
            Time transfer_time{ 0.0 };
            TransferCount transfers{ 0 };
            double fare = 0.0;
            bool has_timed_segment = false;

            for (std::size_t i = 0; i < connection.segments.size(); ++i) {
                const auto segment_id = connection.segments[i];
                const auto segment_index = static_cast<std::size_t>(segment_id.get());
                if (segment_index >= network.connection_segments.size()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("connection references unknown connection segment")
                            .ctx("connection_index", static_cast<std::int64_t>(index))
                            .ctx("segment_position", static_cast<std::int64_t>(i))
                            .ctx("connection_segment_id", segment_id.get())
                    );
                }

                const auto& segment = connection_segment_at(network, segment_id);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                const auto from = physical_from_key(route_segment);
                const auto to = physical_to_key(route_segment);

                if (i == 0) {
                    start = from;
                } else if (*previous_to != from) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("connection segment trace is not physically contiguous")
                            .ctx("connection_index", static_cast<std::int64_t>(index))
                            .ctx("segment_position", static_cast<std::int64_t>(i))
                            .ctx("previous_to_kind", static_cast<std::int64_t>(previous_to->kind))
                            .ctx("previous_to_id", previous_to->id)
                            .ctx("current_from_kind", static_cast<std::int64_t>(from.kind))
                            .ctx("current_from_id", from.id)
                    );
                }
                previous_to = to;
                finish = to;

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
                            .ctx("connection_index", static_cast<std::int64_t>(index))
                            .ctx("segment_position", static_cast<std::int64_t>(i))
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
                .start = start
                , .finish = finish
                , .departure = *departure
                , .arrival = *current_time
                , .journey_time = Time{ current_time->value() - departure->value() }
                , .transfer_time = transfer_time
                , .transfers = transfers
                , .fare = fare
            };
        }

        mathfp::Expected<mathfp::Unit> validate_evaluated_connection_against_declared(
            const DiscoveredConnection& connection
            , const EvaluatedConnectionTrace& evaluated
            , double fare_scale
            , const SearchParams& params
            , std::size_t index
        ) {
            if (evaluated.start.kind != EndpointKind::Zone || evaluated.start.id != connection.origin.get()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not start at declared origin zone")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("origin", connection.origin.get())
                );
            }
            if (evaluated.finish.kind != EndpointKind::Zone || evaluated.finish.id != connection.destination.get()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection trace does not end at declared destination zone")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("destination", connection.destination.get())
                );
            }
            if (!almost_equal_time(connection.departure, evaluated.departure)
                || !almost_equal_time(connection.arrival, evaluated.arrival)
                || !almost_equal_time(connection.journey_time, evaluated.journey_time)
                || !almost_equal_time(connection.transfer_time, evaluated.transfer_time)
                || connection.transfers != evaluated.transfers
                || !almost_equal_scalar(connection.fare, evaluated.fare)) {
                return mathfp::unexpected(
                    mathfp::internal_error("declared connection metrics disagree with segment trace")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("declared_departure", connection.departure.value())
                        .ctx("evaluated_departure", evaluated.departure.value())
                        .ctx("declared_arrival", connection.arrival.value())
                        .ctx("evaluated_arrival", evaluated.arrival.value())
                );
            }

            const auto impedance = expected_search_impedance(
                evaluated.journey_time
                , evaluated.transfers
                , evaluated.fare
                , fare_scale
                , params
            );
            if (!almost_equal_scalar(connection.impedance, impedance)) {
                return mathfp::unexpected(
                    mathfp::internal_error("declared connection impedance disagrees with search formula")
                        .ctx("connection_index", static_cast<std::int64_t>(index))
                        .ctx("declared_impedance", connection.impedance)
                        .ctx("evaluated_impedance", impedance)
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_unique_connection_traces(
            std::span<const DiscoveredConnection> connections
            , std::string_view where
        ) {
            std::map<ConnectionTraceKey, std::size_t> seen;
            for (std::size_t i = 0; i < connections.size(); ++i) {
                auto key = connection_trace_key(connections[i]);
                if (const auto [it, inserted] = seen.emplace(std::move(key), i); !inserted) {
                    return mathfp::unexpected(
                        mathfp::internal_error("duplicate connection trace detected")
                            .ctx("stage", std::string(where))
                            .ctx("first_index", static_cast<std::int64_t>(it->second))
                            .ctx("duplicate_index", static_cast<std::int64_t>(i))
                            .ctx("origin", connections[i].origin.get())
                            .ctx("destination", connections[i].destination.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        std::map<OdKey, std::size_t> count_connections_by_od(
            std::span<const DiscoveredConnection> connections
        ) {
            std::map<OdKey, std::size_t> counts;
            for (const auto& connection : connections) {
                ++counts[od_key(connection)];
            }
            return counts;
        }

        std::map<ConnectionTraceKey, std::size_t> trace_index_map(
            std::span<const DiscoveredConnection> connections
        ) {
            std::map<ConnectionTraceKey, std::size_t> indices;
            for (std::size_t i = 0; i < connections.size(); ++i) {
                indices.emplace(connection_trace_key(connections[i]), i);
            }
            return indices;
        }

        bool intervals_overlap(
            const TimeInterval& lhs
            , const TimeInterval& rhs
        ) noexcept {
            return lhs.start.value() < rhs.end.value()
                && rhs.start.value() < lhs.end.value();
        }

        std::map<IntervalId, const TimeInterval*> build_interval_map(
            const InputModel& input
        ) {
            std::map<IntervalId, const TimeInterval*> intervals;
            for (const auto& interval : input.intervals) {
                intervals.emplace(interval.id, &interval);
            }
            return intervals;
        }

        std::map<ZoneId, const Zone*> build_zone_map(
            const InputModel& input
        ) {
            std::map<ZoneId, const Zone*> zones;
            for (const auto& zone : input.zones) {
                zones.emplace(zone.id, &zone);
            }
            return zones;
        }

        mathfp::Expected<std::map<DemandKey, const DemandEntry*>> validate_and_index_demand_entries(
            const InputModel& input
            , std::span<const DiscoveredConnection> choice_connections
            , bool emit_warnings
        ) {
            const auto intervals = build_interval_map(input);
            const auto zones = build_zone_map(input);
            const auto choice_counts = count_connections_by_od(choice_connections);

            std::map<DemandKey, const DemandEntry*> demand_by_key;
            for (const auto& demand : input.demand) {
                if (!std::isfinite(demand.passengers) || demand.passengers < 0.0) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand passengers must be finite and non-negative")
                            .ctx("origin", demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                            .ctx("passengers", demand.passengers)
                    );
                }
                if (!intervals.contains(demand.interval)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand references unknown time interval")
                            .ctx("origin", demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }
                if (!zones.contains(demand.origin) || !zones.contains(demand.destination)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("demand references unknown zone")
                            .ctx("origin", demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }

                const auto key = demand_key(demand);
                if (!demand_by_key.emplace(key, &demand).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate demand entry for the same origin/destination/interval")
                            .ctx("origin", demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }

                if (emit_warnings && demand.origin == demand.destination) {
                    warn(fmt::format(
                        "split input: demand entry has identical origin and destination zone {} for interval {}"
                        , demand.origin.get()
                        , demand.interval.get()
                    ));
                }
                if (emit_warnings && mathfp::almost_zero(demand.passengers)) {
                    warn(fmt::format(
                        "split input: demand entry origin={} destination={} interval={} has zero passengers"
                        , demand.origin.get()
                        , demand.destination.get()
                        , demand.interval.get()
                    ));
                }
                if (emit_warnings
                    && !choice_counts.contains(OdKey{ demand.origin, demand.destination })
                    && demand.passengers > 0.0) {
                    warn(fmt::format(
                        "split input: no chosen connections for demand origin={} destination={} interval={}"
                        , demand.origin.get()
                        , demand.destination.get()
                        , demand.interval.get()
                    ));
                }
            }

            return demand_by_key;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_input(
        const AssignmentInput& input
    ) {
        const auto has_presegmented = input.presegmented.has_value();
        const auto has_raw_routes = !input.input.routes.empty();
        const auto has_raw_trips = !input.input.trips.empty();
        const auto has_walk_links = !input.input.walk_links.empty();

        if (!has_presegmented && !has_raw_routes && !has_raw_trips && !has_walk_links) {
            return mathfp::unexpected(
                mathfp::invalid_arg("assignment input contains no supply data")
            );
        }

        if (has_presegmented && (has_raw_routes || has_raw_trips || has_walk_links)) {
            warn("preprocessing input: presegmented supply is present; raw routes/trips/walk_links will be ignored");
        }

        if (input.input.intervals.empty() && input.input.demand.empty()) {
            warn("preprocessing input: no intervals or demand entries loaded; full pipeline will fail at split stage");
        } else if (input.input.intervals.empty()) {
            warn("preprocessing input: demand entries are present without time intervals; split stage will fail");
        } else if (input.input.demand.empty()) {
            warn("preprocessing input: time intervals are present without demand entries; split stage will fail");
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_output(
        const PreprocessedNetwork& network
        , const SearchParams& params
    ) {
        if (network.route_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("preprocessing produced no route segments")
            );
        }
        if (network.connection_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("preprocessing produced no connection segments")
            );
        }

        MATHFP_TRY(validate_index_offsets(
            network.route_index.line_offsets
            , network.route_index.line_buckets.size()
            , network.route_index.line_order.size()
            , "route_index.line"
        ));
        MATHFP_TRY(validate_index_offsets(
            network.route_index.walk_offsets
            , network.route_index.walk_buckets.size()
            , network.route_index.walk_order.size()
            , "route_index.walk"
        ));
        MATHFP_TRY(validate_index_offsets(
            network.connection_index.timed_offsets
            , network.connection_index.timed_buckets.size()
            , network.connection_index.timed_order.size()
            , "connection_index.timed"
        ));
        MATHFP_TRY(validate_index_offsets(
            network.connection_index.boarding_offsets
            , network.connection_index.boarding_stop_buckets.size()
            , network.connection_index.boarding_order.size()
            , "connection_index.boarding"
        ));
        MATHFP_TRY(validate_index_offsets(
            network.connection_index.walk_offsets
            , network.connection_index.walk_buckets.size()
            , network.connection_index.walk_order.size()
            , "connection_index.walk"
        ));

        if (network.route_index.line_order.size() + network.route_index.walk_order.size()
            != network.route_segments.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("route index partition does not cover all route segments")
                    .ctx("line_count", static_cast<std::int64_t>(network.route_index.line_order.size()))
                    .ctx("walk_count", static_cast<std::int64_t>(network.route_index.walk_order.size()))
                    .ctx("segment_count", static_cast<std::int64_t>(network.route_segments.size()))
            );
        }
        if (network.connection_index.timed_order.size() + network.connection_index.walk_order.size()
            != network.connection_segments.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("connection index partition does not cover all connection segments")
                    .ctx("timed_count", static_cast<std::int64_t>(network.connection_index.timed_order.size()))
                    .ctx("walk_count", static_cast<std::int64_t>(network.connection_index.walk_order.size()))
                    .ctx("segment_count", static_cast<std::int64_t>(network.connection_segments.size()))
            );
        }
        if (network.connection_index.boarding_order.size() != network.connection_index.timed_order.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("boarding index size does not match timed connection count")
                    .ctx("boarding_count", static_cast<std::int64_t>(network.connection_index.boarding_order.size()))
                    .ctx("timed_count", static_cast<std::int64_t>(network.connection_index.timed_order.size()))
            );
        }

        const auto has_zone_bucket = std::any_of(
            network.connection_index.walk_buckets.begin()
            , network.connection_index.walk_buckets.end()
            , [](const EndpointKey& key) { return key.kind == EndpointKind::Zone; }
        );
        if (!has_zone_bucket) {
            return mathfp::unexpected(
                mathfp::invalid_arg("preprocessing produced no zone endpoints for search origins/destinations")
            );
        }

        const auto fare_required = mathfp::units::as_dimless(params.impedance.a_fare) > 0.0;
        const auto has_any_fare = std::any_of(
            network.connection_segments.begin()
            , network.connection_segments.end()
            , [](const ConnectionSegment& segment) {
                return segment.fare.has_value() && std::isfinite(*segment.fare);
            }
        );
        if (fare_required && !has_any_fare) {
            warn("preprocessing output: fare weight is positive but no connection segment carries fare; fare term will collapse to zero");
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_step_output(
        const ConnectionSearchResult& result
        , const PreprocessedNetwork& network
        , double fare_scale
        , const SearchParams& params
    ) {
        if (!(fare_scale > 0.0) || !std::isfinite(fare_scale)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search fare_scale must be finite and strictly positive")
                    .ctx("fare_scale", fare_scale)
            );
        }

        if (result.connections.empty()) {
            warn("search output: no feasible connections were found");
            return mathfp::kUnit;
        }

        MATHFP_TRY(validate_unique_connection_traces(result.connections, "search"));
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

    mathfp::Expected<mathfp::Unit> validate_choice_step_output(
        const ConnectionChoiceResult& choice_result
        , const ConnectionSearchResult& search_result
    ) {
        if (search_result.connections.empty() && choice_result.connections.empty()) {
            warn("choice output: no search connections were available to prune");
            return mathfp::kUnit;
        }

        MATHFP_TRY(validate_unique_connection_traces(choice_result.connections, "choice"));

        const auto search_trace_map = trace_index_map(search_result.connections);
        for (std::size_t i = 0; i < choice_result.connections.size(); ++i) {
            const auto& connection = choice_result.connections[i];
            if (!search_trace_map.contains(connection_trace_key(connection))) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice output contains a connection that was not present in search output")
                        .ctx("choice_index", static_cast<std::int64_t>(i))
                        .ctx("origin", connection.origin.get())
                        .ctx("destination", connection.destination.get())
                );
            }
        }

        const auto search_groups = count_connections_by_od(search_result.connections);
        const auto choice_groups = count_connections_by_od(choice_result.connections);
        for (const auto& [od, search_count] : search_groups) {
            if (search_count > 0 && !choice_groups.contains(od)) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice step removed every connection from a non-empty OD group")
                        .ctx("origin", od.origin.get())
                        .ctx("destination", od.destination.get())
                        .ctx("search_count", static_cast<std::int64_t>(search_count))
                );
            }
        }

        if (choice_result.connections.empty()) {
            warn("choice output: every searched OD group is empty after pruning");
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_split_step_input(
        const ConnectionChoiceResult& choice_result
        , const InputModel& input
    ) {
        if (input.intervals.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split step requires non-empty time intervals")
            );
        }
        if (input.demand.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split step requires non-empty demand entries")
            );
        }
        
        std::map<IntervalId, const TimeInterval*> intervals;
        for (const auto& interval : input.intervals) {
            if (!intervals.emplace(interval.id, &interval).second) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate time interval id")
                        .ctx("interval_id", interval.id.get())
                );
            }
            if (!(interval.start.value() < interval.end.value())) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("time interval must satisfy start < end")
                        .ctx("interval_id", interval.id.get())
                        .ctx("start", interval.start.value())
                        .ctx("end", interval.end.value())
                );
            }
        }

        for (std::size_t i = 0; i < input.intervals.size(); ++i) {
            for (std::size_t j = i + 1; j < input.intervals.size(); ++j) {
                const auto& lhs = input.intervals[i];
                const auto& rhs = input.intervals[j];
                if (intervals_overlap(lhs, rhs)) {
                    warn(fmt::format(
                        "split input: intervals {} and {} overlap in time"
                        , lhs.id.get()
                        , rhs.id.get()
                    ));
                }
            }
        }

        if (choice_result.connections.empty()) {
            warn("split input: no chosen connections are available; split output will be empty");
        }

        MATHFP_TRY(validate_and_index_demand_entries(input, choice_result.connections, true));
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_split_step_output(
        const DemandSplitResult& split_result
        , const ConnectionChoiceResult& choice_result
        , const InputModel& input
    ) {
        const auto choice_trace_map = trace_index_map(choice_result.connections);
        const auto demand_by_key_result = validate_and_index_demand_entries(
            input
            , choice_result.connections
            , false
        );
        if (!demand_by_key_result) {
            return mathfp::unexpected(std::move(demand_by_key_result.error()));
        }
        const auto& demand_by_key = *demand_by_key_result;
        const auto choice_counts = count_connections_by_od(choice_result.connections);

        std::map<DemandKey, double> probability_sum_by_key;
        std::map<DemandKey, double> passengers_sum_by_key;

        for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
            const auto& share = split_result.shares[i];
            const auto key = DemandKey{
                .origin = share.origin
                , .destination = share.destination
                , .interval = share.interval
            };

            if (!demand_by_key.contains(key)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split output contains a share without matching demand entry")
                        .ctx("share_index", static_cast<std::int64_t>(i))
                        .ctx("origin", share.origin.get())
                        .ctx("destination", share.destination.get())
                        .ctx("interval_id", share.interval.get())
                );
            }
            if (!choice_trace_map.contains(connection_trace_key(share.connection))) {
                return mathfp::unexpected(
                    mathfp::internal_error("split output contains a connection that was not present in choice output")
                        .ctx("share_index", static_cast<std::int64_t>(i))
                        .ctx("origin", share.origin.get())
                        .ctx("destination", share.destination.get())
                        .ctx("interval_id", share.interval.get())
                );
            }
            if (!std::isfinite(share.passengers) || share.passengers < 0.0
                || !valid_probability(share.probability)
                || !std::isfinite(share.independence) || share.independence <= 0.0 || share.independence > 1.0
                || !is_finite_non_negative(share.split_impedance)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split output contains non-finite or out-of-range share metrics")
                        .ctx("share_index", static_cast<std::int64_t>(i))
                        .ctx("passengers", share.passengers)
                        .ctx("probability", share.probability)
                        .ctx("independence", share.independence)
                        .ctx("split_impedance", share.split_impedance)
                );
            }

            probability_sum_by_key[key] += share.probability;
            passengers_sum_by_key[key] += share.passengers;
        }

        for (const auto& [key, demand] : demand_by_key) {
            const auto has_available_choice = choice_counts.contains(OdKey{
                .origin = key.origin
                , .destination = key.destination
            });
            if (!has_available_choice || demand->passengers <= 0.0) {
                continue;
            }

            if (!probability_sum_by_key.contains(key)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split output is missing shares for a demand entry with available chosen connections")
                        .ctx("origin", key.origin.get())
                        .ctx("destination", key.destination.get())
                        .ctx("interval_id", key.interval.get())
                );
            }

            const auto probability_sum = probability_sum_by_key[key];
            const auto passengers_sum = passengers_sum_by_key[key];
            if (!almost_equal_scalar(probability_sum, 1.0)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split probabilities do not sum to one")
                        .ctx("origin", key.origin.get())
                        .ctx("destination", key.destination.get())
                        .ctx("interval_id", key.interval.get())
                        .ctx("probability_sum", probability_sum)
                );
            }
            if (!almost_equal_scalar(passengers_sum, demand->passengers)) {
                return mathfp::unexpected(
                    mathfp::internal_error("split passengers do not conserve demand")
                        .ctx("origin", key.origin.get())
                        .ctx("destination", key.destination.get())
                        .ctx("interval_id", key.interval.get())
                        .ctx("passengers_sum", passengers_sum)
                        .ctx("demand_passengers", demand->passengers)
                );
            }
        }

        if (split_result.shares.empty()) {
            warn("split output: no demand shares were produced");
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
