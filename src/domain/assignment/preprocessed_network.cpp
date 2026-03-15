#include "timetable/domain/assignment/preprocessed_network.hpp"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/statistics.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/domain/segments_order.hpp"
#include "timetable/domain/preprocessing/connection_segments.hpp"
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

        mathfp::Expected<std::vector<RouteSegment>> canonicalize_route_segments(
            std::vector<RouteSegment> route_segments
            , bool allow_empty
        ) {
            MATHFP_TRY(validate_route_segments(route_segments, allow_empty));
            return reindex_route_segments(std::move(route_segments));
        }

        mathfp::Expected<PreprocessedNetwork> finalize_preprocessed_network(
            std::vector<RouteSegment> route_segments
            , std::vector<ConnectionSegment> connection_segments
            , bool allow_empty
        ) {
            MATHFP_TRY_LET(
                std::vector<RouteSegment>
                , canonical_route_segments
                , canonicalize_route_segments(std::move(route_segments), allow_empty)
            );
            if (connection_segments.empty() && !allow_empty) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segments collection is empty")
                );
            }
            // TODO: Canonicalize/validate connection_segments here as well so that
            // both preprocessing paths establish the same full network invariants.

            using SegmentIndices = std::pair<
                preprocessing::RouteSegmentIndex
                , preprocessing::ConnectionSegmentIndex
            >;
            MATHFP_TRY_LET(
                SegmentIndices
                , indices
                , build_indices(canonical_route_segments, connection_segments)
            );

            timetable::infra::progress::both("preprocessing: done");

            return PreprocessedNetwork{
                .route_segments        = std::move(canonical_route_segments)
                , .connection_segments = std::move(connection_segments)
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
