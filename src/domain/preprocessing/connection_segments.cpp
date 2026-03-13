#include "timetable/domain/preprocessing/connection_segments.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_map>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/domain/preprocessing/segments_factory.hpp"
#include "timetable/domain/segments_order.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::preprocessing {

    namespace {

        using StopIndex = std::unordered_map<StopId, std::size_t>;

        StopIndex index_stop_times(const Trip& trip) {
            StopIndex idx;
            idx.reserve(trip.times.size());
            for (std::size_t i = 0; i < trip.times.size(); ++i) {
                idx.emplace(trip.times[i].stop, i);
            }
            return idx;
        }

        struct IndexedTrip final {
            const Trip* trip{};
            StopIndex   stop_index{};
        };

        using IndexedTripsByLine = std::map<LineId, std::vector<IndexedTrip>>;
        using StopEndpoints = std::pair<StopId, StopId>;

        struct ConnectionBuildStats final {
            std::size_t walk_segments{};
            std::size_t timed_segments{};
            std::size_t skipped_missing_routes{};
            std::size_t skipped_invalid_endpoints{};
            std::size_t skipped_missing_trips{};
            std::size_t skipped_missing_trip_stops{};
            std::size_t skipped_invalid_trip_order{};
            std::size_t skipped_early_arrival{};
            std::size_t skipped_overnight_policy{};
        };

        const Route* find_route(
            const std::unordered_map<RouteId, const Route*>& routes_by_id
            , RouteId id
        ) {
            const auto it = routes_by_id.find(id);
            return it == routes_by_id.end() ? nullptr : it->second;
        }

        mathfp::Expected<const Route*> find_trip_route(
            const Trip& trip
            , const std::unordered_map<RouteId, const Route*>& routes_by_id
            , const PreprocessParams& params
            , ConnectionBuildStats& stats
        ) {
            const auto* route = find_route(routes_by_id, trip.route);
            if (route) {
                return route;
            }

            ++stats.skipped_missing_routes;
            if (params.strict_trips) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("trip references unknown route")
                    .ctx("trip_id", trip.id.get())
                    .ctx("route_id", trip.route.get())
                );
            }
            return static_cast<const Route*>(nullptr);
        }

        IndexedTrip index_trip(const Trip& trip) {
            return IndexedTrip{
                .trip = &trip,
                .stop_index = index_stop_times(trip)
            };
        }

        void insert_indexed_trip(
            IndexedTripsByLine& trips_by_line
            , LineId line
            , IndexedTrip indexed_trip
        ) {
            trips_by_line[line].push_back(std::move(indexed_trip));
        }

        mathfp::Expected<IndexedTripsByLine> group_trips_by_line(
            const std::vector<Route>& routes
            , const std::vector<Trip>& trips
            , const PreprocessParams& params
            , ConnectionBuildStats& stats
        ) {
            std::unordered_map<RouteId, const Route*> routes_by_id;
            routes_by_id.reserve(routes.size());
            for (const auto& r : routes) {
                routes_by_id.emplace(r.id, &r);
            }

            IndexedTripsByLine out;
            for (const auto& trip : trips) {
                MATHFP_TRY_LET(
                    const Route*
                    , route
                    , find_trip_route(trip, routes_by_id, params, stats)
                );
                if (!route) {
                    continue;
                }
                insert_indexed_trip(out, route->line, index_trip(trip));
            }
            return out;
        }

        std::vector<const RouteSegment*> ordered_route_segments(
            const std::vector<RouteSegment>& route_segments
            , bool stable_ordering
        ) {
            std::vector<const RouteSegment*> route_order;
            route_order.reserve(route_segments.size());
            std::transform(
                route_segments.begin(), route_segments.end(),
                std::back_inserter(route_order),
                [](const RouteSegment& rs) { return &rs; }
            );
            if (stable_ordering) {
                std::sort(route_order.begin(), route_order.end(), [](const RouteSegment* a, const RouteSegment* b) {
                    return route_segment_less(*a, *b);
                });
            }
            return route_order;
        }

        IndexedTripsByLine ordered_line_trips(
            IndexedTripsByLine trips_by_line
            , bool stable_ordering
        ) {
            if (!stable_ordering)
                return trips_by_line;

            for (auto& [line, list] : trips_by_line) {
                std::sort(list.begin(), list.end(), [](const IndexedTrip& a, const IndexedTrip& b) {
                    return a.trip->id.get() < b.trip->id.get();
                });
            }
            return trips_by_line;
        }

        struct OrderedConnectionGenerationInputs final {
            std::vector<const RouteSegment*>         route_order{};
            IndexedTripsByLine trips_by_line{};
        };

        OrderedConnectionGenerationInputs ordered_generation_inputs(
            const std::vector<RouteSegment>& route_segments
            , IndexedTripsByLine trips_by_line
            , bool stable_ordering
        ) {
            // Deterministic ConnectionSegmentId assignment is defined here:
            // route segments are generated in ordered_route_segments(...) order,
            // and timed segments for each line follow ordered_line_trips(...).
            return OrderedConnectionGenerationInputs{
                .route_order = ordered_route_segments(route_segments, stable_ordering),
                .trips_by_line = ordered_line_trips(std::move(trips_by_line), stable_ordering)
            };
        }

        struct TimedSegmentData final {
            Time arrival{};
            Time departure{};
            std::int64_t from_index{};
            std::int64_t to_index{};
        };

        mathfp::Expected<std::optional<TimedSegmentData>> extract_trip_times(
            const IndexedTrip& indexed_trip
            , StopId from
            , StopId to
            , const PreprocessParams& params
            , ConnectionBuildStats& stats
        ) {
            const auto& trip = *indexed_trip.trip;
            const auto it_from = indexed_trip.stop_index.find(from);
            const auto it_to = indexed_trip.stop_index.find(to);
            if (it_from == indexed_trip.stop_index.end() || it_to == indexed_trip.stop_index.end()) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("trip does not contain route segment stops")
                        .ctx("trip_id", trip.id.get())
                        .ctx("from", from.get())
                        .ctx("to", to.get())
                    );
                }
                ++stats.skipped_missing_trip_stops;
                return std::optional<TimedSegmentData>{};
            }

            if (it_to->second <= it_from->second) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("route segment order invalid in trip")
                        .ctx("trip_id", trip.id.get())
                        .ctx("from", from.get())
                        .ctx("to", to.get())
                    );
                }
                ++stats.skipped_invalid_trip_order;
                return std::optional<TimedSegmentData>{};
            }

            const auto& dep = trip.times[it_from->second].departure;
            auto arr = trip.times[it_to->second].arrival;
            if (arr.value() < dep.value()) {
                if (!params.allow_overnight) {
                    if (params.strict_trips) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("arrival earlier than departure")
                            .ctx("trip_id", trip.id.get())
                            .ctx("from", from.get())
                            .ctx("to", to.get())
                        );
                    }
                    ++stats.skipped_early_arrival;
                    return std::optional<TimedSegmentData>{};
                }
                if (params.overnight_add_24h) {
                    arr = Time{ arr.value() + 24.0 * 3600.0 };
                } else {
                    if (params.strict_trips) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("overnight arrival requires add_24h policy")
                            .ctx("trip_id", trip.id.get())
                            .ctx("from", from.get())
                            .ctx("to", to.get())
                        );
                    }
                    ++stats.skipped_overnight_policy;
                    return std::optional<TimedSegmentData>{};
                }
            }

            return std::optional<TimedSegmentData>{ TimedSegmentData{
                .arrival = arr,
                .departure = dep,
                .from_index = static_cast<std::int64_t>(it_from->second),
                .to_index = static_cast<std::int64_t>(it_to->second)
            } };
        }

        bool has_non_strict_skips(const ConnectionBuildStats& stats) noexcept {
            return stats.skipped_missing_routes > 0
                || stats.skipped_invalid_endpoints > 0
                || stats.skipped_missing_trips > 0
                || stats.skipped_missing_trip_stops > 0
                || stats.skipped_invalid_trip_order > 0
                || stats.skipped_early_arrival > 0
                || stats.skipped_overnight_policy > 0;
        }

        void log_skip_summary(const ConnectionBuildStats& stats) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;

            log(
                fmt::format(
                    "connection segments: skipped_missing_routes = {:>6}  skipped_invalid_endpoints = {:>6}  skipped_missing_trips = {:>6}\n"
                    "                    skipped_missing_trip_stops = {:>6}  skipped_invalid_trip_order = {:>6}\n"
                    "                    skipped_early_arrival = {:>6}  skipped_overnight_policy = {:>6}"
                    , stats.skipped_missing_routes
                    , stats.skipped_invalid_endpoints
                    , stats.skipped_missing_trips
                    , stats.skipped_missing_trip_stops
                    , stats.skipped_invalid_trip_order
                    , stats.skipped_early_arrival
                    , stats.skipped_overnight_policy
                ),
                LogLevel::Info
            );

            if (has_non_strict_skips(stats)) {
                log(
                    "connection segments: non-strict mode skipped invalid input rows/trips; see counters above",
                    LogLevel::Warning
                );
            }
        }

        void log_connection_segment_totals(
            const std::vector<ConnectionSegment>& segments
            , const ConnectionBuildStats& stats
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;

            log(
                fmt::format(
                    "connection segments: walk = {:>8}  timed = {:>8}  total = {:>8}"
                    , stats.walk_segments
                    , stats.timed_segments
                    , segments.size()
                ),
                LogLevel::Info
            );
        }

        mathfp::Expected<ConnectionSegment> build_walk_connection_segment(
            const RouteSegment& route_segment
            , std::int64_t& next_id
        ) {
            return make_connection_segment(
                ConnectionSegmentId{ next_id++ }
                , route_segment
                , std::nullopt
                , std::nullopt
                , std::nullopt
                , std::nullopt
                , std::nullopt
                , std::nullopt
            );
        }

        mathfp::Expected<std::optional<StopEndpoints>> extract_line_route_stop_endpoints(
            const RouteSegment& route_segment
            , const PreprocessParams& params
            , ConnectionBuildStats& stats
        ) {
            if (!std::holds_alternative<StopId>(route_segment.from)
                || !std::holds_alternative<StopId>(route_segment.to)) {
                ++stats.skipped_invalid_endpoints;
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("line route segment endpoints must be stops")
                        .ctx("route_segment_id", route_segment.id.get())
                    );
                }
                return std::optional<StopEndpoints>{};
            }

            return std::optional<StopEndpoints>{ StopEndpoints{
                std::get<StopId>(route_segment.from),
                std::get<StopId>(route_segment.to)
            } };
        }

        mathfp::Expected<ConnectionSegment> build_timed_connection_segment(
            const RouteSegment& route_segment
            , const IndexedTrip& indexed_trip
            , const TimedSegmentData& times
            , std::int64_t& next_id
        ) {
            return make_connection_segment(
                ConnectionSegmentId{ next_id++ }
                , route_segment
                , indexed_trip.trip->id
                , times.from_index
                , times.to_index
                , times.departure
                , times.arrival
                , std::nullopt
            );
        }

        mathfp::Expected<std::vector<ConnectionSegment>> build_timed_connection_segments_for_route(
            const RouteSegment& route_segment
            , const std::vector<IndexedTrip>& line_trips
            , const PreprocessParams& params
            , std::int64_t& next_id
            , ConnectionBuildStats& stats
        ) {
            MATHFP_TRY_LET(
                std::optional<StopEndpoints>
                , stop_endpoints
                , extract_line_route_stop_endpoints(route_segment, params, stats)
            );
            if (!stop_endpoints.has_value()) {
                return std::vector<ConnectionSegment>{};
            }

            // TODO: Derive a better reserve estimate for timed segments when
            // preprocessing large networks; line_trips.size() is only a loose upper bound.
            std::vector<ConnectionSegment> segments;
            segments.reserve(line_trips.size());
            for (const auto& indexed_trip : line_trips) {
                MATHFP_TRY_LET(
                    std::optional<TimedSegmentData>
                    , times
                    , extract_trip_times(
                        indexed_trip
                        , stop_endpoints->first
                        , stop_endpoints->second
                        , params
                        , stats
                    )
                );
                if (!times.has_value()) {
                    continue;
                }

                ++stats.timed_segments;
                MATHFP_TRY_LET(
                    ConnectionSegment
                    , segment
                    , build_timed_connection_segment(
                        route_segment
                        , indexed_trip
                        , *times
                        , next_id
                    )
                );
                // TODO: Decide how fare should be derived for built-from-trips
                // timed segments; presegmented CSV can carry fare explicitly,
                // but this path currently leaves fare unset by construction.
                segments.push_back(std::move(segment));
            }

            return segments;
        }

        mathfp::Expected<mathfp::Unit> append_route_connection_segments(
            std::vector<ConnectionSegment>& out
            , const RouteSegment& route_segment
            , const IndexedTripsByLine& trips_by_line
            , const PreprocessParams& params
            , std::int64_t& next_id
            , ConnectionBuildStats& stats
        ) {
            if (is_walk(route_segment.carrier)) {
                ++stats.walk_segments;
                MATHFP_TRY_LET(
                    ConnectionSegment
                    , segment
                    , build_walk_connection_segment(route_segment, next_id)
                );
                out.push_back(std::move(segment));
                return mathfp::kUnit;
            }

            const auto line = std::get<LineId>(route_segment.carrier);
            const auto it_trips = trips_by_line.find(line);
            if (it_trips == trips_by_line.end()) {
                ++stats.skipped_missing_trips;
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("line has no trips")
                        .ctx("line_id", line.get())
                    );
                }
                return mathfp::kUnit;
            }

            MATHFP_TRY_LET(
                std::vector<ConnectionSegment>
                , route_segments
                , build_timed_connection_segments_for_route(
                    route_segment
                    , it_trips->second
                    , params
                    , next_id
                    , stats
                )
            );
            out.insert(
                out.end()
                , std::make_move_iterator(route_segments.begin())
                , std::make_move_iterator(route_segments.end())
            );
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<std::vector<ConnectionSegment>> build_connection_segments(
        const std::vector<RouteSegment>& route_segments
        , const std::vector<Route>& routes
        , const std::vector<Trip>& trips
        , const PreprocessParams& params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: connection segments (timed + walk)");
        log(
            fmt::format(
                "connection segments input: route_segments = {:>8}  routes = {:>6}  trips = {:>6}"
                , route_segments.size()
                , routes.size()
                , trips.size()
            )
            , LogLevel::Info
        );

        std::vector<ConnectionSegment> out;
        std::int64_t next_id = 0;
        ConnectionBuildStats stats;

        MATHFP_TRY_LET(
            IndexedTripsByLine
            , trips_by_line
            , group_trips_by_line(routes, trips, params, stats)
        );
        const auto ordered_inputs = ordered_generation_inputs(
            route_segments
            , std::move(trips_by_line)
            , params.stable_ordering
        );
        log(
            fmt::format(
                "connection segments params: stable_ordering = {}  strict_trips = {}  allow_overnight = {}  add_24h = {}"
                , params.stable_ordering   ? "true" : "false"
                , params.strict_trips      ? "true" : "false"
                , params.allow_overnight   ? "true" : "false"
                , params.overnight_add_24h ? "true" : "false"
            )
            , LogLevel::Info
        );

        for (const auto* route_segment : ordered_inputs.route_order) {
            MATHFP_TRY(append_route_connection_segments(
                out
                , *route_segment
                , ordered_inputs.trips_by_line
                , params
                , next_id
                , stats
            ));
        }

        log_connection_segment_totals(out, stats);
        log_skip_summary(stats);
        both("preprocessing: connection segments done");

        return out;
    }

}  // namespace timetable::domain::preprocessing
