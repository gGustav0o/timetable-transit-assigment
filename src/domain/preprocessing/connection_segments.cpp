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
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/domain/state_ops.hpp"
#include "timetable/domain/segments_order.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::preprocessing {

    namespace {

        struct IndexedTrip final {
            const Trip* trip{};
        };

        using IndexedTripsByLine  = std::map<LineId, std::vector<IndexedTrip>>;
        using LinesById           = std::unordered_map<LineId, const Line*>;
        using OccurrenceEndpoints = std::pair<StopOccurrence, StopOccurrence>;

        struct ConnectionBuildStats final {
            std::size_t walk_segments{};
            std::size_t timed_segments{};
            std::size_t skipped_missing_routes{};
            std::size_t skipped_non_line_topology{};
            std::size_t skipped_missing_trips{};
            std::size_t skipped_missing_trip_stops{};
            std::size_t skipped_invalid_trip_order{};
            std::size_t skipped_early_arrival{};
            std::size_t skipped_overnight_policy{};
        };

        struct ConnectionBuildState final {
            std::vector<ConnectionSegment> segments{};
            std::int64_t                   next_id{};
            ConnectionBuildStats           stats{};
        };

        struct GroupTripsResult final {
            IndexedTripsByLine   trips_by_line{};
            ConnectionBuildStats stats{};
        };

        struct RouteLookupResult final {
            const Route*         route{};
            ConnectionBuildStats stats{};
        };

        struct OccurrenceEndpointResult final {
            std::optional<OccurrenceEndpoints> endpoints{};
            ConnectionBuildStats               stats{};
        };

        struct TimedSegmentData final {
            Time          arrival{};
            Time          departure{};
            RoutePosition from_index{};
            RoutePosition to_index{};
        };

        struct TripTimesResult final {
            std::optional<TimedSegmentData> times{};
            ConnectionBuildStats            stats{};
        };

        struct TimedRouteBuildResult final {
            std::vector<ConnectionSegment> segments{};
            std::int64_t                   next_id{};
            ConnectionBuildStats           stats{};
        };

        struct BuiltConnectionSegment final {
            ConnectionSegment segment{};
            std::int64_t      next_id{};
        };

        ConnectionBuildStats add_stats(
              ConnectionBuildStats        lhs
            , const ConnectionBuildStats& rhs
        ) {
            lhs.walk_segments += rhs.walk_segments;
            lhs.timed_segments += rhs.timed_segments;
            lhs.skipped_missing_routes += rhs.skipped_missing_routes;
            lhs.skipped_non_line_topology += rhs.skipped_non_line_topology;
            lhs.skipped_missing_trips += rhs.skipped_missing_trips;
            lhs.skipped_missing_trip_stops += rhs.skipped_missing_trip_stops;
            lhs.skipped_invalid_trip_order += rhs.skipped_invalid_trip_order;
            lhs.skipped_early_arrival += rhs.skipped_early_arrival;
            lhs.skipped_overnight_policy += rhs.skipped_overnight_policy;
            return lhs;
        }

        mathfp::Expected<LinesById> index_lines_by_id(
            const std::vector<Line>& lines
        ) {
            LinesById indexed;
            indexed.reserve(lines.size());
            for (const auto& line : lines) {
                const auto [it, inserted] = indexed.emplace(line.id, &line);
                (void)it;
                if (!inserted) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate line id in raw input")
                            .ctx("line_id", line.id.get())
                    );
                }
            }
            return indexed;
        }

        const Line* find_line(
              const LinesById& lines_by_id
            , LineId           id
        ) {
            const auto it = lines_by_id.find(id);
            return it == lines_by_id.end() ? nullptr : it->second;
        }

        const Route* find_route(
              const std::unordered_map<RouteId, const Route*>& routes_by_id
            , RouteId                                          id
        ) {
            const auto it = routes_by_id.find(id);
            return it == routes_by_id.end() ? nullptr : it->second;
        }

        mathfp::Expected<RouteLookupResult> find_trip_route(
              const Trip&                                      trip
            , const std::unordered_map<RouteId, const Route*>& routes_by_id
            , const PreprocessParams&                          params
        ) {
            const auto* route = find_route(routes_by_id, trip.route);
            if (route) {
                return RouteLookupResult{
                      .route = route
                    , .stats = {}
                };
            }

            if (params.strict_trips) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("trip references unknown route")
                    .ctx("trip_id" , trip.id.get())
                    .ctx("route_id", trip.route.get())
                );
            }
            return RouteLookupResult{
                  .route = nullptr
                , .stats = ConnectionBuildStats{ .skipped_missing_routes = 1 }
            };
        }

        IndexedTrip index_trip(const Trip& trip) {
            return IndexedTrip{
                .trip = &trip
            };
        }

        IndexedTripsByLine insert_indexed_trip(
              IndexedTripsByLine trips_by_line
            , LineId             line
            , IndexedTrip        indexed_trip
        ) {
            trips_by_line[line].push_back(std::move(indexed_trip));
            return trips_by_line;
        }

        mathfp::Expected<GroupTripsResult> group_trips_by_line(
              const std::vector<Route>& routes
            , const std::vector<Trip>&  trips
            , const PreprocessParams&   params
        ) {
            std::unordered_map<RouteId, const Route*> routes_by_id;
            routes_by_id.reserve(routes.size());
            for (const auto& r : routes) {
                routes_by_id.emplace(r.id, &r);
            }

            IndexedTripsByLine out;
            ConnectionBuildStats stats;
            for (const auto& trip : trips) {
                MATHFP_TRY_LET(
                      RouteLookupResult
                    , route_result
                    , find_trip_route(trip, routes_by_id, params)
                );
                stats = add_stats(std::move(stats), route_result.stats);
                if (!route_result.route) {
                    continue;
                }
                out = insert_indexed_trip(std::move(out), route_result.route->line, index_trip(trip));
            }
            return GroupTripsResult{
                  .trips_by_line = std::move(out)
                , .stats         = std::move(stats)
            };
        }

        std::vector<const RouteSegment*> ordered_route_segments(
              const std::vector<RouteSegment>& route_segments
            , bool                             stable_ordering
        ) {
            std::vector<const RouteSegment*> route_order;
            route_order.reserve(route_segments.size());
            std::transform(
                  route_segments.begin()
                , route_segments.end()
                , std::back_inserter(route_order)
                , [](const RouteSegment& rs) { return &rs; }
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
            , bool               stable_ordering
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
            std::vector<const RouteSegment*> route_order{};
            IndexedTripsByLine               trips_by_line{};
        };

        OrderedConnectionGenerationInputs ordered_generation_inputs(
              const std::vector<RouteSegment>& route_segments
            , IndexedTripsByLine               trips_by_line
            , bool                             stable_ordering
        ) {
            //tex:
            // The article requires the route-segment array to be sorted before
            // connection-segment generation. With stable ordering enabled this function
            // defines the deterministic order $$(y_1,\ldots,y_m)$$ used to instantiate
            // walk segments once and timed ride segments for every compatible trip.
            // Deterministic ConnectionSegmentId assignment is defined here:
            // route segments are generated in ordered_route_segments(...) order
            // and timed segments for each line follow ordered_line_trips(...).
            return OrderedConnectionGenerationInputs{
                  .route_order   = ordered_route_segments(route_segments, stable_ordering)
                , .trips_by_line = ordered_line_trips(std::move(trips_by_line), stable_ordering)
            };
        }

        mathfp::Expected<TripTimesResult> extract_trip_times(
              const IndexedTrip&      indexed_trip
            , StopOccurrence          from
            , StopOccurrence          to
            , const PreprocessParams& params
        ) {
            const auto& trip      = *indexed_trip.trip;
            const auto from_index = static_cast<std::size_t>(from.position.get());
            const auto to_index   = static_cast<std::size_t>(to.position.get());

            if (to_index <= from_index) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("route segment occurrence order invalid in trip")
                        .ctx("trip_id"      , trip.id.get())
                        .ctx("from_stop_id" , from.stop.get())
                        .ctx("from_position", from.position.get())
                        .ctx("to_stop_id"   , to.stop.get())
                        .ctx("to_position"  , to.position.get())
                    );
                }
                return TripTimesResult{
                      .times = std::nullopt
                    , .stats = ConnectionBuildStats{ .skipped_invalid_trip_order = 1 }
                };
            }

            if (to_index >= trip.times.size()) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("trip does not contain route segment occurrences")
                        .ctx("trip_id"             , trip.id.get())
                        .ctx("trip_stop_time_count", static_cast<std::int64_t>(trip.times.size()))
                        .ctx("from_stop_id"        , from.stop.get())
                        .ctx("from_position"       , from.position.get())
                        .ctx("to_stop_id"          , to.stop.get())
                        .ctx("to_position"         , to.position.get())
                    );
                }
                return TripTimesResult{
                      .times = std::nullopt
                    , .stats = ConnectionBuildStats{ .skipped_missing_trip_stops = 1 }
                };
            }

            if (trip.times[from_index].stop != from.stop || trip.times[to_index].stop != to.stop) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("trip stop occurrences do not match route segment occurrences")
                        .ctx("trip_id"          , trip.id.get())
                        .ctx("from_stop_id"     , from.stop.get())
                        .ctx("from_position"    , from.position.get())
                        .ctx("trip_from_stop_id", trip.times[from_index].stop.get())
                        .ctx("to_stop_id"       , to.stop.get())
                        .ctx("to_position"      , to.position.get())
                        .ctx("trip_to_stop_id"  , trip.times[to_index].stop.get())
                    );
                }
                return TripTimesResult{
                      .times = std::nullopt
                    , .stats = ConnectionBuildStats{ .skipped_missing_trip_stops = 1 }
                };
            }

            const auto& dep = trip.times[from_index].departure;
            auto arr        = trip.times[to_index].arrival;
            if (arr.value() < dep.value()) {
                if (!params.allow_overnight) {
                    if (params.strict_trips) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("arrival earlier than departure")
                            .ctx("trip_id"      , trip.id.get())
                            .ctx("from_stop_id" , from.stop.get())
                            .ctx("from_position", from.position.get())
                            .ctx("to_stop_id"   , to.stop.get())
                            .ctx("to_position"  , to.position.get())
                        );
                    }
                    return TripTimesResult{
                          .times = std::nullopt
                        , .stats = ConnectionBuildStats{ .skipped_early_arrival = 1 }
                    };
                }
                if (params.overnight_add_24h) {
                    arr = Time{ arr.value() + 24.0 * 3600.0 };
                } else {
                    if (params.strict_trips) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("overnight arrival requires add_24h policy")
                            .ctx("trip_id"      , trip.id.get())
                            .ctx("from_stop_id" , from.stop.get())
                            .ctx("from_position", from.position.get())
                            .ctx("to_stop_id"   , to.stop.get())
                            .ctx("to_position"  , to.position.get())
                        );
                    }
                    return TripTimesResult{
                          .times = std::nullopt
                        , .stats = ConnectionBuildStats{ .skipped_overnight_policy = 1 }
                    };
                }
            }

            return TripTimesResult{
                .times      = TimedSegmentData{
                      .arrival    = arr
                    , .departure  = dep
                    , .from_index = from.position
                    , .to_index   = to.position
                }
                , .stats = {}
            };
        }

        bool has_trip_occurrences(
              const IndexedTrip& indexed_trip
            , StopOccurrence     from
            , StopOccurrence     to
        ) {
            const auto& trip      = *indexed_trip.trip;
            const auto from_index = static_cast<std::size_t>(from.position.get());
            const auto to_index   = static_cast<std::size_t>(to.position.get());
            if (to_index <= from_index || to_index >= trip.times.size()) {
                return false;
            }
            return trip.times[from_index].stop == from.stop
                && trip.times[to_index].stop == to.stop;
        }

        std::size_t estimate_timed_segment_reserve(
              const std::vector<IndexedTrip>& line_trips
            , StopOccurrence                  from
            , StopOccurrence                  to
        ) {
            return static_cast<std::size_t>(std::count_if(
                  line_trips.begin()
                , line_trips.end()
                , [&](const IndexedTrip& indexed_trip) {
                    return has_trip_occurrences(indexed_trip, from, to);
                }
            ));
        }

        bool has_non_strict_skips(const ConnectionBuildStats& stats) noexcept {
            return stats.skipped_missing_routes > 0
                || stats.skipped_non_line_topology > 0
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
                    "connection segments: skipped_missing_routes = {:>6}  skipped_non_line_topology = {:>6}  skipped_missing_trips = {:>6}\n"
                    "                    skipped_missing_trip_stops = {:>6}  skipped_invalid_trip_order = {:>6}\n"
                    "                    skipped_early_arrival = {:>6}  skipped_overnight_policy = {:>6}"
                    , stats.skipped_missing_routes
                    , stats.skipped_non_line_topology
                    , stats.skipped_missing_trips
                    , stats.skipped_missing_trip_stops
                    , stats.skipped_invalid_trip_order
                    , stats.skipped_early_arrival
                    , stats.skipped_overnight_policy
                )
                , LogLevel::Info
            );

            if (has_non_strict_skips(stats)) {
                log(
                      "connection segments: non-strict mode skipped invalid input rows/trips; see counters above"
                    , LogLevel::Warning
                );
            }
        }

        void log_connection_segment_totals(
              const std::vector<ConnectionSegment>& segments
            , const ConnectionBuildStats&           stats
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;

            log(
                fmt::format(
                      "connection segments: walk = {:>8}  timed = {:>8}  total = {:>8}"
                    , stats.walk_segments
                    , stats.timed_segments
                    , segments.size()
                )
                , LogLevel::Info
            );
        }

        mathfp::Expected<BuiltConnectionSegment> build_walk_connection_segment(
              const RouteSegment& route_segment
            , std::int64_t        next_id
        ) {
            //tex:
            // A walk connection segment is the always-available instantiation of
            // a walk route segment:
            // $$s=(y,\varnothing,\varnothing),\qquad t_{\mathrm{dep}}(s),t_{\mathrm{arr}}(s)\ \text{are not fixed}.$$
            MATHFP_TRY_LET(
                  ConnectionSegment
                , segment
                , make_connection_segment(
                      ConnectionSegmentId{ next_id++ }
                    , route_segment
                    , std::nullopt
                    , std::nullopt
                    , std::nullopt
                    , std::nullopt
                    , std::nullopt
                    , std::nullopt
                )
            );
            return BuiltConnectionSegment{
                  .segment = std::move(segment)
                , .next_id = next_id
            };
        }

        mathfp::Expected<OccurrenceEndpointResult> extract_line_route_occurrences(
              const RouteSegment&     route_segment
            , const PreprocessParams& params
        ) {
            const auto* line = line_topology_of(route_segment);
            if (!line) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("timed connection generation requires line route topology")
                        .ctx("route_segment_id", route_segment.id.get())
                    );
                }
                return OccurrenceEndpointResult{
                      .endpoints = std::nullopt
                    , .stats     = ConnectionBuildStats{ .skipped_non_line_topology = 1 }
                };
            }

            return OccurrenceEndpointResult{
                .endpoints = OccurrenceEndpoints{
                      line->from
                    , line->to
                }
                , .stats = {}
            };
        }

        mathfp::Expected<BuiltConnectionSegment> build_timed_connection_segment(
              const RouteSegment&     route_segment
            , const IndexedTrip&      indexed_trip
            , const TimedSegmentData& times
            , double                  fare
            , std::int64_t            next_id
        ) {
            //tex:
            // A transit connection segment is a route segment instantiated by
            // one compatible trip:
            // $$s=(y,\mathrm{trip},t_{\mathrm{dep}},t_{\mathrm{arr}}).$$
            // The route segment supplies the transfer-free line ride topology;
            // the trip stop times supply the concrete departure and arrival.
            MATHFP_TRY_LET(
                  ConnectionSegment
                , segment
                , make_connection_segment(
                      ConnectionSegmentId{ next_id++ }
                    , route_segment
                    , indexed_trip.trip->id
                    , times.from_index
                    , times.to_index
                    , times.departure
                    , times.arrival
                    , fare
                )
            );
            return BuiltConnectionSegment{
                  .segment = std::move(segment)
                , .next_id = next_id
            };
        }

        mathfp::Expected<double> required_line_ride_fare(
            const Line& line
        ) {
            if (!line.fare.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("raw timed connection generation requires explicit line fare")
                        .ctx("line_id", line.id.get())
                );
            }

            const auto valid = ensure_line_fare_consistent(*line.fare);
            if (!valid) {
                auto error = valid.error();
                return mathfp::unexpected(
                    std::move(error).ctx("line_id", line.id.get())
                );
            }

            return line_ride_fare_value(*line.fare);
        }

        mathfp::Expected<double> timed_route_segment_fare(
              const RouteSegment& route_segment
            , const LinesById&    lines_by_id
        ) {
            const auto line_id = line_of(route_segment);
            if (!line_id.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("timed route segment fare requires line topology")
                        .ctx("route_segment_id", route_segment.id.get())
                );
            }

            const auto* line = find_line(lines_by_id, *line_id);
            if (line == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("route segment references unknown line fare")
                        .ctx("route_segment_id", route_segment.id.get())
                        .ctx("line_id"         , line_id->get())
                );
            }

            MATHFP_TRY_LET(double, fare, required_line_ride_fare(*line));
            return fare;
        }

        mathfp::Expected<TimedRouteBuildResult> build_timed_connection_segments_for_route(
              const RouteSegment&             route_segment
            , const std::vector<IndexedTrip>& line_trips
            , const LinesById&                lines_by_id
            , const PreprocessParams&         params
            , std::int64_t                    next_id
        ) {
            MATHFP_TRY_LET(
                  OccurrenceEndpointResult
                , occurrence_result
                , extract_line_route_occurrences(route_segment, params)
            );

            ConnectionBuildStats stats = std::move(occurrence_result.stats);
            if (!occurrence_result.endpoints.has_value()) {
                return TimedRouteBuildResult{
                      .segments = {}
                    , .next_id  = next_id
                    , .stats    = std::move(stats)
                };
            }

            MATHFP_TRY_LET(
                  double
                , segment_fare
                , timed_route_segment_fare(route_segment, lines_by_id)
            );

            std::vector<ConnectionSegment> segments;
            segments.reserve(estimate_timed_segment_reserve(
                  line_trips
                , occurrence_result.endpoints->first
                , occurrence_result.endpoints->second
            ));
            const auto route = route_of(route_segment).value();
            for (const auto& indexed_trip : line_trips) {
                if (indexed_trip.trip->route != route) {
                    continue;
                }

                MATHFP_TRY_LET(
                      TripTimesResult
                    , trip_times_result
                    , extract_trip_times(
                          indexed_trip
                        , occurrence_result.endpoints->first
                        , occurrence_result.endpoints->second
                        , params
                    )
                );
                stats = add_stats(std::move(stats), trip_times_result.stats);
                if (!trip_times_result.times.has_value()) {
                    continue;
                }

                stats.timed_segments += 1;
                MATHFP_TRY_LET(
                      BuiltConnectionSegment
                    , built_segment
                    , build_timed_connection_segment(
                          route_segment
                        , indexed_trip
                        , *trip_times_result.times
                        , segment_fare
                        , next_id
                    )
                );
                next_id = built_segment.next_id;
                segments.push_back(std::move(built_segment.segment));
            }

            return TimedRouteBuildResult{
                  .segments = std::move(segments)
                , .next_id  = next_id
                , .stats    = std::move(stats)
            };
        }

        mathfp::Expected<ConnectionBuildState> append_route_connection_segments(
              ConnectionBuildState      state
            , const RouteSegment&       route_segment
            , const IndexedTripsByLine& trips_by_line
            , const LinesById&          lines_by_id
            , const PreprocessParams&   params
        ) {
            if (is_walk(route_segment)) {
                state.stats.walk_segments += 1;
                MATHFP_TRY_LET(
                      BuiltConnectionSegment
                    , built_segment
                    , build_walk_connection_segment(route_segment, state.next_id)
                );
                state.next_id  = built_segment.next_id;
                state.segments.push_back(std::move(built_segment.segment));
                return state;
            }

            const auto line     = line_of(route_segment).value();
            const auto it_trips = trips_by_line.find(line);
            if (it_trips == trips_by_line.end()) {
                if (params.strict_trips) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("line has no trips")
                        .ctx("line_id", line.get())
                    );
                }
                state.stats.skipped_missing_trips += 1;
                return state;
            }

            MATHFP_TRY_LET(
                  TimedRouteBuildResult
                , route_result
                , build_timed_connection_segments_for_route(
                      route_segment
                    , it_trips->second
                    , lines_by_id
                    , params
                    , state.next_id
                )
            );
            state.next_id  = route_result.next_id;
            state.stats    = add_stats(std::move(state.stats), route_result.stats);
            state.segments.insert(
                  state.segments.end()
                , std::make_move_iterator(route_result.segments.begin())
                , std::make_move_iterator(route_result.segments.end())
            );
            return state;
        }

    }  // namespace

    mathfp::Expected<std::vector<ConnectionSegment>> build_connection_segments(
          const std::vector<RouteSegment>& route_segments
        , const std::vector<Line>&         lines
        , const std::vector<Route>&        routes
        , const std::vector<Trip>&         trips
        , const PreprocessParams&          params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: connection segments (timed + walk)");
        log(
            fmt::format(
                  "connection segments input: route_segments = {:>8}  lines = {:>6}  routes = {:>6}  trips = {:>6}"
                , route_segments.size()
                , lines.size()
                , routes.size()
                , trips.size()
            )
            , LogLevel::Info
        );

        MATHFP_TRY_LET(
              GroupTripsResult
            , grouped_trips
            , group_trips_by_line(routes, trips, params)
        );
        const auto ordered_inputs = ordered_generation_inputs(
              route_segments
            , std::move(grouped_trips.trips_by_line)
            , params.stable_ordering
        );
        MATHFP_TRY_LET(
              LinesById
            , lines_by_id
            , index_lines_by_id(lines)
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

        ConnectionBuildState state{
              .segments = {}
            , .next_id  = 0
            , .stats    = std::move(grouped_trips.stats)
        };

        MATHFP_TRY_LET(
              ConnectionBuildState
            , built_state
            , timetable::domain::state_ops::fold(
                  std::move(state)
                , ordered_inputs.route_order
                , [&](ConnectionBuildState current, const RouteSegment* route_segment) {
                    return append_route_connection_segments(
                          std::move(current)
                        , *route_segment
                        , ordered_inputs.trips_by_line
                        , lines_by_id
                        , params
                    );
                }
            )
        );
        state = std::move(built_state);

        log_connection_segment_totals(state.segments, state.stats);
        log_skip_summary(state.stats);
        both("preprocessing: connection segments done");

        return std::move(state.segments);
    }

}  // namespace timetable::domain::preprocessing
