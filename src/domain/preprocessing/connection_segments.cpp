#include "timetable/domain/preprocessing/connection_segments.hpp"

#include <algorithm>
#include <iterator>
#include <map>
#include <unordered_map>
#include <utility>

#include <mathfp/core/error.hpp>

#include <fmt/format.h>

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

        const Route* find_route(
            const std::unordered_map<RouteId, const Route*>& routes_by_id
            , RouteId id
        ) {
            const auto it = routes_by_id.find(id);
            return it == routes_by_id.end() ? nullptr : it->second;
        }

        std::map<LineId, std::vector<const Trip*>> group_trips_by_line(
            const std::vector<Route>& routes
            , const std::vector<Trip>& trips
        ) {
            std::unordered_map<RouteId, const Route*> routes_by_id;
            routes_by_id.reserve(routes.size());
            for (const auto& r : routes) {
                routes_by_id.emplace(r.id, &r);
            }

            std::map<LineId, std::vector<const Trip*>> out;
            for (const auto& trip : trips) {
                const auto* route = find_route(routes_by_id, trip.route);
                if (!route)
                    continue;
                out[route->line].push_back(&trip);
            }
            return out;
        }

        void sort_trips_by_id(
            std::map<LineId, std::vector<const Trip*>>& trips_by_line
            , bool stable_ordering
        ) {
            if (!stable_ordering)
                return;
            for (auto& [line, list] : trips_by_line) {
                std::sort(list.begin(), list.end(), [](const Trip* a, const Trip* b) {
                    return a->id.get() < b->id.get();
                });
            }
        }

        std::vector<const RouteSegment*> make_route_order(
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

        mathfp::Expected<std::optional<std::pair<Time, Time>>> extract_trip_times(
            const Trip& trip
            , StopId from
            , StopId to
            , const PreprocessParams& params
        ) {
            const auto index = index_stop_times(trip);
            const auto it_from = index.find(from);
            const auto it_to = index.find(to);
            if (it_from == index.end() || it_to == index.end()) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("trip does not contain route segment stops")
                        .ctx("trip_id", trip.id.get())
                        .ctx("from", from.get())
                        .ctx("to", to.get())
                    );
                }
                return std::optional<std::pair<Time, Time>>{};
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
                return std::optional<std::pair<Time, Time>>{};
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
                    return std::optional<std::pair<Time, Time>>{};
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
                    return std::optional<std::pair<Time, Time>>{};
                }
            }

            return std::optional<std::pair<Time, Time>>{ std::pair<Time, Time>{ dep, arr } };
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

        auto trips_by_line = group_trips_by_line(routes, trips);
        sort_trips_by_id(trips_by_line, params.stable_ordering);
        const auto route_order = make_route_order(route_segments, params.stable_ordering);
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

        std::size_t walk_segments             = 0;
        std::size_t timed_segments            = 0;
        std::size_t skipped_missing_trips     = 0;
        std::size_t skipped_invalid_endpoints = 0;

        for (const auto* rs_ptr : route_order) {
            const auto& rs = *rs_ptr;
            if (is_walk(rs.carrier)) {
                ++walk_segments;
                out.push_back(ConnectionSegment{
                    .id              = ConnectionSegmentId{ next_id++ }
                    , .route_segment = rs.id
                    , .departure     = std::nullopt
                    , .arrival       = std::nullopt
                    , .fare          = std::nullopt
                });
                continue;
            }

            const auto line = std::get<LineId>(rs.carrier);
            if (!std::holds_alternative<StopId>(rs.from) || !std::holds_alternative<StopId>(rs.to)) {
                ++skipped_invalid_endpoints;
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("line route segment endpoints must be stops")
                        .ctx("route_segment_id", rs.id.get())
                    );
                }
                continue;
            }
            const auto it_trips = trips_by_line.find(line);
            if (it_trips == trips_by_line.end()) {
                ++skipped_missing_trips;
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("line has no trips")
                        .ctx("line_id", line.get())
                    );
                }
                continue;
            }

            const auto from_stop = std::get<StopId>(rs.from);
            const auto to_stop   = std::get<StopId>(rs.to);
            for (const auto* trip : it_trips->second) {
                const auto times = extract_trip_times(*trip, from_stop, to_stop, params);
                if (!times)
                    return mathfp::unexpected(times.error());
                if (!times.value())
                    continue;

                ++timed_segments;
                out.push_back(ConnectionSegment{
                    .id              = ConnectionSegmentId{ next_id++ }
                    , .route_segment = rs.id
                    , .departure     = times->value().first
                    , .arrival       = times->value().second
                    , .fare          = std::nullopt
                });
            }
        }

        log(
            fmt::format(
                "connection segments: walk = {:>8}  timed = {:>8}  total = {:>8}"
                , walk_segments
                , timed_segments
                , out.size()
            ),
            LogLevel::Info
        );
        log(
            fmt::format(
                "connection segments: skipped_invalid_endpoints = {:>6}  skipped_missing_trips = {:>6}"
                , skipped_invalid_endpoints
                , skipped_missing_trips
            ),
            LogLevel::Info
        );
        both("preprocessing: connection segments done");

        return out;
    }

}  // namespace timetable::domain::preprocessing
