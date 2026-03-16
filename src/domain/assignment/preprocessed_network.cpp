#include "timetable/domain/assignment/preprocessed_network.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/statistics.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/domain/segments_order.hpp"
#include "timetable/domain/preprocessing/connection_segments.hpp"
#include "timetable/domain/preprocessing/segments_factory.hpp"
#include "timetable/domain/preprocessing/route_segments.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        std::vector<double> collect_present_fares(
            std::span<const ConnectionSegment> segments
        ) {
            std::vector<double> fares;
            fares.reserve(segments.size());
            for (const auto& segment : segments) {
                if (segment.fare && std::isfinite(*segment.fare)) {
                    fares.push_back(*segment.fare);
                }
            }
            return fares;
        }

        struct RouteSegmentKey final {
            EndpointKey             from{};
            EndpointKey             to{};
            CarrierKind             kind{};
            std::int64_t            carrier_id{};
            std::vector<WalkLinkId> path{};

            auto operator<=>(const RouteSegmentKey&) const = default;
        };

        struct ConnectionSegmentKey final {
            std::int64_t route_segment{};
            std::optional<std::int64_t> trip{};
            std::optional<std::int64_t> from_index{};
            std::optional<std::int64_t> to_index{};
            std::optional<double> departure{};
            std::optional<double> arrival{};
            std::optional<double> fare{};

            auto operator<=>(const ConnectionSegmentKey&) const = default;
        };

        struct CanonicalRouteSegments final {
            std::vector<RouteSegment> routes{};
            std::unordered_map<std::int64_t, RouteSegmentId> old_to_new_ids{};
        };

        RouteSegmentKey route_segment_key(const RouteSegment& segment) {
            const auto kind = carrier_kind(segment.carrier);
            const auto carrier_id = is_line(segment.carrier)
                ? std::get<LineId>(segment.carrier).get()
                : 0;
            std::vector<WalkLinkId> path{};
            if (is_walk(segment.carrier)) {
                path = std::get<WalkPath>(segment.carrier);
            }

            return RouteSegmentKey{
                .from = to_endpoint_key(segment.from),
                .to = to_endpoint_key(segment.to),
                .kind = kind,
                .carrier_id = carrier_id,
                .path = std::move(path)
            };
        }

        std::vector<RouteSegmentKey> collect_route_segment_keys(
            const std::vector<RouteSegment>& segments
        ) {
            std::vector<RouteSegmentKey> keys;
            keys.reserve(segments.size());
            std::transform(
                segments.begin()
                , segments.end()
                , std::back_inserter(keys)
                , route_segment_key
            );
            return keys;
        }

        const RouteSegmentKey* find_duplicate_route_segment_key(
            std::vector<RouteSegmentKey>& keys
        ) {
            std::sort(keys.begin(), keys.end());
            const auto duplicate = std::adjacent_find(keys.begin(), keys.end());
            return duplicate == keys.end() ? nullptr : &*duplicate;
        }

        mathfp::Expected<mathfp::Unit> validate_unique_route_segment_ids(
            std::span<const RouteSegment> segments
        ) {
            std::vector<std::int64_t> ids;
            ids.reserve(segments.size());
            for (const auto& segment : segments) {
                ids.push_back(segment.id.get());
            }

            std::sort(ids.begin(), ids.end());
            const auto duplicate = std::adjacent_find(ids.begin(), ids.end());
            if (duplicate == ids.end()) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("duplicate route segment id before canonicalization")
                    .ctx("route_segment_id", *duplicate)
            );
        }

        ConnectionSegmentKey connection_segment_key(
            const ConnectionSegment& segment
        ) {
            return ConnectionSegmentKey{
                .route_segment = segment.route_segment.get(),
                .trip = segment.trip ? std::optional<std::int64_t>{ segment.trip->get() } : std::nullopt,
                .from_index = segment.from_index,
                .to_index = segment.to_index,
                .departure = segment.departure ? std::optional<double>{ segment.departure->value() } : std::nullopt,
                .arrival = segment.arrival ? std::optional<double>{ segment.arrival->value() } : std::nullopt,
                .fare = segment.fare
            };
        }

        std::vector<ConnectionSegmentKey> collect_connection_segment_keys(
            const std::vector<ConnectionSegment>& segments
        ) {
            std::vector<ConnectionSegmentKey> keys;
            keys.reserve(segments.size());
            std::transform(
                segments.begin(),
                segments.end(),
                std::back_inserter(keys),
                connection_segment_key
            );
            return keys;
        }

        const ConnectionSegmentKey* find_duplicate_connection_segment_key(
            std::vector<ConnectionSegmentKey>& keys
        ) {
            std::sort(keys.begin(), keys.end());
            const auto duplicate = std::adjacent_find(keys.begin(), keys.end());
            return duplicate == keys.end() ? nullptr : &*duplicate;
        }

        void log_input_sizes(const InputModel& input) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            log(
                fmt::format(
                    "input sizes: stops = {:>6}  zones = {:>6}  lines = {:>6}  routes = {:>6}\n"
                    "             trips = {:>6}  walk_links = {:>6}  intervals = {:>6}  demand = {:>6}"
                    , input.stops.size()
                    , input.zones.size()
                    , input.lines.size()
                    , input.routes.size()
                    , input.trips.size()
                    , input.walk_links.size()
                    , input.intervals.size()
                    , input.demand.size()
                ),
                LogLevel::Info
            );
        }

        mathfp::Expected<std::vector<RouteSegment>> build_route_segments(
            const InputModel& input
            , const PreprocessParams& params
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::both;
            using timetable::infra::progress::log;

            both("preprocessing: building route segments");
            log_input_sizes(input);

            MATHFP_TRY_LET(
                std::vector<RouteSegment>
                , line_segments
                , preprocessing::build_line_route_segments(
                    input.routes, input.trips, input.stops, params
                )
            );
            both("preprocessing: building walk segments");
            MATHFP_TRY_LET(
                std::vector<RouteSegment>
                , walk_segments
                , preprocessing::build_walk_route_segments(
                    input.walk_links, params
                )
            );
            log(
                fmt::format(
                    "route segments: line = {:>8}  walk = {:>8}  total = {:>8}"
                    , line_segments.size()
                    , walk_segments.size()
                    , line_segments.size() + walk_segments.size()
                ),
                LogLevel::Info
            );

            std::vector<RouteSegment> route_segments;
            route_segments.reserve(line_segments.size() + walk_segments.size());
            route_segments.insert(
                route_segments.end()
                , std::make_move_iterator(line_segments.begin())
                , std::make_move_iterator(line_segments.end())
            );
            route_segments.insert(
                route_segments.end()
                , std::make_move_iterator(walk_segments.begin())
                , std::make_move_iterator(walk_segments.end())
            );
            if (params.stable_ordering) {
                std::sort(route_segments.begin(), route_segments.end(), route_segment_less);
            }
            log(
                fmt::format(
                    "route segments: stable_ordering = {}"
                    , params.stable_ordering ? "true" : "false"
                ),
                LogLevel::Info
            );
            MATHFP_TRY(validate_route_segments(route_segments, true));
            return route_segments;
        }

        mathfp::Expected<std::vector<ConnectionSegment>> build_connection_segments(
            const std::vector<RouteSegment>& route_segments
            , const InputModel& input
            , const PreprocessParams& params
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::both;
            using timetable::infra::progress::log;

            both("preprocessing: building connection segments");
            MATHFP_TRY_LET(
                std::vector<ConnectionSegment>
                , connection_segments
                , preprocessing::build_connection_segments(
                    route_segments, input.routes, input.trips, params
                )
            );
            log(
                fmt::format(
                    "connection segments: total = {:>8}"
                    , connection_segments.size()
                ),
                LogLevel::Info
            );
            return connection_segments;
        }

        mathfp::Expected<std::pair<preprocessing::RouteSegmentIndex, preprocessing::ConnectionSegmentIndex>>
        build_indices(
            const std::vector<RouteSegment>& route_segments
            , const std::vector<ConnectionSegment>& connection_segments
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::both;
            using timetable::infra::progress::log;

            both("preprocessing: building indices");
            MATHFP_TRY_LET(
                preprocessing::RouteSegmentIndex
                , route_index
                , preprocessing::build_route_segment_index(route_segments)
            );
            MATHFP_TRY_LET(
                preprocessing::ConnectionSegmentIndex
                , connection_index
                , preprocessing::build_connection_segment_index(
                    connection_segments, route_segments
                )
            );
            log(
                fmt::format(
                    "indices: route_order = {:>8}  route_buckets = {:>6}\n"
                    "         timed_order = {:>8}  timed_buckets = {:>6}\n"
                    "         walk_order  = {:>8}  walk_buckets  = {:>6}"
                    , route_index.order.size()
                    , route_index.buckets.size()
                    , connection_index.timed_order.size()
                    , connection_index.timed_buckets.size()
                    , connection_index.walk_order.size()
                    , connection_index.walk_buckets.size()
                ),
                LogLevel::Info
            );
            return std::pair<preprocessing::RouteSegmentIndex, preprocessing::ConnectionSegmentIndex>{
                std::move(route_index), std::move(connection_index)
            };
        }

        mathfp::Expected<CanonicalRouteSegments> canonicalize_route_segments(
            std::vector<RouteSegment> route_segments
            , bool allow_empty
        ) {
            MATHFP_TRY(validate_route_segments(route_segments, allow_empty));
            MATHFP_TRY(validate_unique_route_segment_ids(route_segments));

            CanonicalRouteSegments out;
            out.routes = std::move(route_segments);
            out.old_to_new_ids.reserve(out.routes.size());
            for (std::size_t i = 0; i < out.routes.size(); ++i) {
                const auto old_id = out.routes[i].id.get();
                const auto new_id = RouteSegmentId{ static_cast<std::int64_t>(i) };
                out.old_to_new_ids.emplace(old_id, new_id);
                out.routes[i].id = new_id;
            }

            return out;
        }

        mathfp::Expected<std::vector<ConnectionSegment>> canonicalize_connection_segments(
            std::vector<ConnectionSegment> connection_segments
            , std::span<const RouteSegment> canonical_route_segments
            , const std::unordered_map<std::int64_t, RouteSegmentId>& route_id_map
            , bool allow_empty
        ) {
            for (std::size_t i = 0; i < connection_segments.size(); ++i) {
                const auto remapped = route_id_map.find(connection_segments[i].route_segment.get());
                if (remapped == route_id_map.end()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("connection segment references unknown route segment")
                            .ctx("connection_segment_id", connection_segments[i].id.get())
                            .ctx("route_segment_id", connection_segments[i].route_segment.get())
                    );
                }
                connection_segments[i].route_segment = remapped->second;
                connection_segments[i].id = ConnectionSegmentId{
                    static_cast<std::int64_t>(i)
                };
            }

            MATHFP_TRY(validate_connection_segments(
                connection_segments
                , canonical_route_segments
                , allow_empty
            ));

            return connection_segments;
        }

        mathfp::Expected<PreprocessedNetwork> finalize_preprocessed_network(
            std::vector<RouteSegment> route_segments
            , std::vector<ConnectionSegment> connection_segments
            , bool allow_empty
        ) {
            MATHFP_TRY_LET(
                CanonicalRouteSegments
                , canonical_routes
                , canonicalize_route_segments(std::move(route_segments), allow_empty)
            );
            MATHFP_TRY_LET(
                std::vector<ConnectionSegment>
                , canonical_connection_segments
                , canonicalize_connection_segments(
                    std::move(connection_segments)
                    , canonical_routes.routes
                    , canonical_routes.old_to_new_ids
                    , allow_empty
                )
            );

            using SegmentIndices = std::pair<
                preprocessing::RouteSegmentIndex
                , preprocessing::ConnectionSegmentIndex
            >;
            MATHFP_TRY_LET(
                SegmentIndices
                , indices
                , build_indices(canonical_routes.routes, canonical_connection_segments)
            );

            timetable::infra::progress::both("preprocessing: done");

            return PreprocessedNetwork{
                .route_segments        = std::move(canonical_routes.routes)
                , .connection_segments = std::move(canonical_connection_segments)
                , .route_index         = std::move(indices.first)
                , .connection_index    = std::move(indices.second)
            };
        }

    }  // namespace

    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network(
        const InputModel& input
        , const PreprocessParams& params
    ) {
        MATHFP_TRY_LET(
            std::vector<RouteSegment>
            , route_segments
            , build_route_segments(input, params)
        );
        MATHFP_TRY_LET(
            std::vector<ConnectionSegment>
            , connection_segments
            , build_connection_segments(route_segments, input, params)
        );

        return finalize_preprocessed_network(
            std::move(route_segments),
            std::move(connection_segments),
            true
        );
    }

    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network_from_segments(
        std::vector<RouteSegment> route_segments
        , std::vector<ConnectionSegment> connection_segments
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: building indices");
        log(
            fmt::format(
                "preprocessing input: route_segments = {:>8}  connection_segments = {:>8}"
                , route_segments.size()
                , connection_segments.size()
            ),
            LogLevel::Info
        );

        return finalize_preprocessed_network(
            std::move(route_segments),
            std::move(connection_segments),
            false
        );
    }

    std::vector<RouteSegment> reindex_route_segments(std::vector<RouteSegment> segments) {
        for (std::size_t i = 0; i < segments.size(); ++i) {
            segments[i].id = RouteSegmentId{ static_cast<std::int64_t>(i) };
        }
        return segments;
    }

    mathfp::Expected<mathfp::Unit> validate_route_segments(
        const std::vector<RouteSegment>& segments
        , bool allow_empty
    ) {
        if (segments.empty() && !allow_empty) {
            return mathfp::unexpected(
                mathfp::invalid_arg("route segments collection is empty")
            );
        }

        auto keys = collect_route_segment_keys(segments);
        if (const auto* duplicate = find_duplicate_route_segment_key(keys)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("duplicate route segments detected")
                .ctx("from", duplicate->from.id)
                .ctx("to", duplicate->to.id)
                .ctx("carrier_id", duplicate->carrier_id)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_connection_segments(
        const std::vector<ConnectionSegment>& segments
        , std::span<const RouteSegment> route_segments
        , bool allow_empty
    ) {
        if (segments.empty() && !allow_empty) {
            return mathfp::unexpected(
                mathfp::invalid_arg("connection segments collection is empty")
            );
        }

        if (!segments.empty() && route_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("connection segments require non-empty route segments")
            );
        }

        std::vector<const RouteSegment*> canonical_routes_by_id(route_segments.size(), nullptr);
        for (const auto& route_segment : route_segments) {
            const auto route_index = static_cast<std::size_t>(route_segment.id.get());
            if (route_index >= canonical_routes_by_id.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("route segment id out of canonical range")
                        .ctx("route_segment_id", route_segment.id.get())
                );
            }
            canonical_routes_by_id[route_index] = &route_segment;
        }

        std::vector<bool> seen_ids(segments.size(), false);
        for (const auto& segment : segments) {
            const auto id_index = static_cast<std::size_t>(segment.id.get());
            if (id_index >= seen_ids.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segment id out of range")
                        .ctx("connection_segment_id", segment.id.get())
                );
            }
            if (seen_ids[id_index]) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate connection segment id")
                        .ctx("connection_segment_id", segment.id.get())
                );
            }
            seen_ids[id_index] = true;

            const auto route_index = static_cast<std::size_t>(segment.route_segment.get());
            if (route_index >= canonical_routes_by_id.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segment route reference out of range")
                        .ctx("connection_segment_id", segment.id.get())
                        .ctx("route_segment_id", segment.route_segment.get())
                );
            }
            const auto* route_segment = canonical_routes_by_id[route_index];
            if (route_segment == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segment references missing canonical route segment")
                        .ctx("connection_segment_id", segment.id.get())
                        .ctx("route_segment_id", segment.route_segment.get())
                );
            }

            MATHFP_TRY_LET(
                ConnectionSegment
                , validated_segment
                , preprocessing::make_connection_segment(
                    segment.id,
                    *route_segment,
                    segment.trip,
                    segment.from_index,
                    segment.to_index,
                    segment.departure,
                    segment.arrival,
                    segment.fare
                )
            );
            (void)validated_segment;
        }

        auto keys = collect_connection_segment_keys(segments);
        if (const auto* duplicate = find_duplicate_connection_segment_key(keys)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("duplicate connection segments detected")
                    .ctx("route_segment_id", duplicate->route_segment)
            );
        }

        return mathfp::kUnit;
    }

    double compute_fare_scale(
        std::span<const ConnectionSegment> segments
        , const FareNormalization& normalization
    ) {
        using Kind = FareNormalization::Kind;
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        if (normalization.kind == Kind::None) {
            log("fare normalization: disabled", LogLevel::Info);
            return 1.0;
        }
        if (normalization.kind == Kind::FixedScale) {
            log("fare normalization: fixed scale", LogLevel::Info);
            return normalization.fixed_scale > 0.0 ? normalization.fixed_scale : 1.0;
        }

        auto fares = collect_present_fares(segments);
        if (fares.empty()) {
            log("fare normalization: no fares present; scale = 1", LogLevel::Info);
            return 1.0;
        }

        if (normalization.kind == Kind::Mean) {
            const auto scale_result = statistics::mean(fares, "fare");
            if (!scale_result) {
                log(
                    fmt::format("fare normalization: mean failed: {}", scale_result.error().message()),
                    LogLevel::Warning
                );
                return 1.0;
            }
            const auto scale = *scale_result;
            log(fmt::format("fare normalization: mean scale = {:.6f}", scale), LogLevel::Info);
            return scale;
        }

        if (normalization.kind == Kind::Median) {
            const auto scale_result = statistics::median(std::move(fares), "fare");
            if (!scale_result) {
                log(
                    fmt::format("fare normalization: median failed: {}", scale_result.error().message()),
                    LogLevel::Warning
                );
                return 1.0;
            }
            const auto scale = *scale_result;
            log(fmt::format("fare normalization: median scale = {:.6f}", scale), LogLevel::Info);
            return scale;
        }

        if (normalization.kind == Kind::P95) {
            const auto scale_result = statistics::p95(std::move(fares), "fare");
            if (!scale_result) {
                log(
                    fmt::format("fare normalization: p95 failed: {}", scale_result.error().message()),
                    LogLevel::Warning
                );
                return 1.0;
            }
            const auto scale = *scale_result;
            log(fmt::format("fare normalization: p95 scale = {:.6f}", scale), LogLevel::Info);
            return scale;
        }

        return 1.0;
    }

}  // namespace timetable::domain::assignment
