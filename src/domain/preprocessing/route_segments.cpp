#include "timetable/domain/preprocessing/route_segments.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <map>
#include <unordered_map>
#include <utility>

#include <boost/container_hash/hash.hpp>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/graph/types.hpp>
#include <mathfp/algorithms/dijkstra.hpp>
#include <mathfp/types/index.hpp>

#include <fmt/format.h>

#include "timetable/domain/preprocessing/segments_factory.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::preprocessing {

    namespace {

        using Pair              = std::pair<std::size_t, std::size_t>;
        using StopPairDurations = std::map<std::pair<StopId, StopId>, std::vector<double>>;
        using EdgeMetrics       = std::pair<std::vector<Time>, std::vector<Length>>;
        using TripsByRoute      = std::map<RouteId, std::vector<const Trip*>>;

        struct PairHash final {
            std::size_t operator()(const Pair& p) const noexcept {
                std::size_t seed = 0;
                boost::hash_combine(seed, p.first);
                boost::hash_combine(seed, p.second);
                return seed;
            }
        };

        mathfp::Expected<mathfp::Unit> ensure_nonneg_duration(
            const Time& duration
            , StopId from
            , StopId to
        ) {
            const auto v = duration.value();
            if (!std::isfinite(v))
                return mathfp::unexpected(
                    mathfp::domain_error("non-finite duration")
                    .ctx("from", from.get())
                    .ctx("to", to.get())
                );
            if (v < 0.0)
                return mathfp::unexpected(
                    mathfp::invalid_arg("negative duration")
                    .ctx("from", from.get())
                    .ctx("to", to.get())
                    .ctx("duration", v)
                );
            return mathfp::kUnit;
        }

        mathfp::Expected<Time> mean_time(const std::vector<double>& values) {
            if (values.empty())
                return mathfp::unexpected(
                    mathfp::invalid_arg("cannot compute mean of empty duration list")
                );
            double sum = 0.0;
            for (const auto v : values) {
                if (!std::isfinite(v))
                    return mathfp::unexpected(
                        mathfp::domain_error("non-finite duration")
                        .ctx("duration", v)
                    );
                sum += v;
            }
            return Time{ sum / static_cast<double>(values.size()) };
        }

        mathfp::Expected<Time> median_time(std::vector<double> values) {
            if (values.empty())
                return mathfp::unexpected(
                    mathfp::invalid_arg("cannot compute median of empty duration list")
                );
            for (const auto v : values) {
                if (!std::isfinite(v))
                    return mathfp::unexpected(
                        mathfp::domain_error("non-finite duration")
                        .ctx("duration", v)
                    );
            }
            const auto mid = values.size() / 2;
            std::nth_element(values.begin(), values.begin() + mid, values.end());
            if (values.size() % 2 == 1)
                return Time{ values[mid] };
            const auto upper = values[mid];
            std::nth_element(values.begin(), values.begin() + (mid - 1), values.end());
            const auto lower = values[mid - 1];
            return Time{ 0.5 * (lower + upper) };
        }

        mathfp::Expected<Time> min_time(const std::vector<double>& values) {
            if (values.empty())
                return mathfp::unexpected(
                    mathfp::invalid_arg("cannot compute min of empty duration list")
                );
            double best = std::numeric_limits<double>::infinity();
            for (const auto v : values) {
                if (!std::isfinite(v))
                    return mathfp::unexpected(
                        mathfp::domain_error("non-finite duration")
                        .ctx("duration", v)
                    );
                if (v < best) best = v;
            }
            return Time{ best };
        }

        mathfp::Expected<Time> aggregate_time(
            std::vector<double> values
            , TimeAggregationKind kind
        ) {
            switch (kind) {
            case TimeAggregationKind::Mean:
                return mean_time(values);
            case TimeAggregationKind::Median:
                return median_time(std::move(values));
            case TimeAggregationKind::Minimum:
                return min_time(values);
            }
            return mathfp::unexpected(
                mathfp::invalid_arg("unknown time aggregation kind")
            );
        }

        TripsByRoute group_trips_by_route(
            const std::vector<Trip>& trips
        ) {
            TripsByRoute trips_by_route;
            for (const auto& trip : trips) {
                trips_by_route[trip.route].push_back(&trip);
            }
            return trips_by_route;
        }

        TripsByRoute ordered_route_trips(
            TripsByRoute trips_by_route
            , bool stable_ordering
        ) {
            if (!stable_ordering)
                return trips_by_route;
            for (auto& [route_id, list] : trips_by_route) {
                std::sort(list.begin(), list.end(), [](const Trip* a, const Trip* b) {
                    return a->id.get() < b->id.get();
                });
            }
            return trips_by_route;
        }

        std::vector<const Route*> ordered_routes(
            const std::vector<Route>& routes
            , bool stable_ordering
        ) {
            std::vector<const Route*> route_order;
            route_order.reserve(routes.size());
            std::transform(
                routes.begin(), routes.end(),
                std::back_inserter(route_order),
                [](const Route& r) { return &r; }
            );
            if (stable_ordering) {
                std::sort(route_order.begin(), route_order.end(), [](const Route* a, const Route* b) {
                    return a->id.get() < b->id.get();
                });
            }
            return route_order;
        }

        using TripStopIndex = std::map<StopId, std::size_t>;

        TripStopIndex index_trip_stop_times(const Trip& trip) {
            TripStopIndex index_by_stop;
            for (std::size_t i = 0; i < trip.times.size(); ++i) {
                index_by_stop.emplace(trip.times[i].stop, i);
            }
            return index_by_stop;
        }

        mathfp::Expected<std::optional<Time>> consecutive_stop_duration(
            const Trip& trip
            , const TripStopIndex& index_by_stop
            , StopId from
            , StopId to
            , RouteId route_id
        ) {
            const auto it_from = index_by_stop.find(from);
            const auto it_to = index_by_stop.find(to);
            if (it_from == index_by_stop.end() || it_to == index_by_stop.end()) {
                return std::optional<Time>{};
            }
            if (it_to->second <= it_from->second) {
                return std::optional<Time>{};
            }

            const auto& dep = trip.times[it_from->second].departure;
            const auto& arr = trip.times[it_to->second].arrival;
            const auto duration = Time{ arr.value() - dep.value() };
            if (!ensure_nonneg_duration(duration, from, to)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("invalid trip timing for consecutive stops")
                    .ctx("route_id", route_id.get())
                    .ctx("from", from.get())
                    .ctx("to", to.get())
                );
            }

            return std::optional<Time>{ duration };
        }

        mathfp::Expected<mathfp::Unit> append_trip_durations(
            StopPairDurations& durations
            , const Route& route
            , const Trip& trip
        ) {
            const auto index_by_stop = index_trip_stop_times(trip);
            const auto& route_stops = route.stops;
            for (std::size_t k = 0; k + 1 < route_stops.size(); ++k) {
                const auto from = route_stops[k];
                const auto to = route_stops[k + 1];
                MATHFP_TRY_LET(
                    std::optional<Time>
                    , duration
                    , consecutive_stop_duration(
                        trip
                        , index_by_stop
                        , from
                        , to
                        , route.id
                    )
                );
                if (!duration.has_value()) {
                    continue;
                }
                durations[{ from, to }].push_back(duration->value());
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<StopPairDurations> collect_durations(
            const Route& route
            , const std::vector<const Trip*>& trips
        ) {
            StopPairDurations durations;

            for (const auto* trip : trips) {
                MATHFP_TRY(append_trip_durations(durations, route, *trip));
            }

            return durations;
        }

        mathfp::Expected<EdgeMetrics> build_edge_metrics(
            const Route& route
            , const StopPairDurations& durations
            , const PreprocessParams& params
        ) {
            const auto& r_stops = route.stops;
            std::vector<Time> edge_time(r_stops.size() - 1, Time{ 0.0 });
            std::vector<Length> edge_len(r_stops.size() - 1, Length{ 0.0 });

            for (std::size_t k = 0; k + 1 < r_stops.size(); ++k) {
                const auto from = r_stops[k];
                const auto to = r_stops[k + 1];
                const auto it = durations.find({ from, to });
                if (it == durations.end()) {
                    if (params.strict_stop_times) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("missing running time for consecutive stops")
                            .ctx("route_id", route.id.get())
                            .ctx("from", from.get())
                            .ctx("to", to.get())
                        );
                    }
                    return mathfp::unexpected(mathfp::invalid_arg("skip"));
                }

                MATHFP_TRY_LET(
                    Time
                    , m
                    , aggregate_time(it->second, params.time_aggregation)
                );
                edge_time[k] = m;
                if (params.line_speed) {
                    const auto len = (*params.line_speed) * edge_time[k];
                    edge_len[k] = Length{ len.value() };
                } else {
                    edge_len[k] = Length{ 0.0 };
                }
            }

            return EdgeMetrics{
                std::move(edge_time), std::move(edge_len)
            };
        }

        std::pair<std::vector<Time>, std::vector<Length>> build_cumulative_metrics(
            const std::vector<Time>& edge_time
            , const std::vector<Length>& edge_len
        ) {
            const auto n = edge_time.size() + 1;
            std::vector<Time> cum_time(n, Time{ 0.0 });
            std::vector<Length> cum_len(n, Length{ 0.0 });
            for (std::size_t i = 1; i < n; ++i) {
                cum_time[i] = Time{ cum_time[i - 1].value() + edge_time[i - 1].value() };
                cum_len[i] = Length{ cum_len[i - 1].value() + edge_len[i - 1].value() };
            }
            return { std::move(cum_time), std::move(cum_len) };
        }

        struct BestEdge final {
            double     weight{};
            WalkLinkId id{};
        };

        struct WalkGraphData final {
            std::vector<WalkEndpoint> endpoints{};
            std::unordered_map<EndpointKey, std::size_t> index_by_key{};
            std::unordered_map<Pair, BestEdge, PairHash> best_edges{};
            std::unordered_map<WalkLinkId, const WalkLink*> link_by_id{};
        };

        struct WalkKey final {
            EndpointKey             from{};
            EndpointKey             to{};
            std::vector<WalkLinkId> path{};

            auto operator<=>(const WalkKey&) const = default;
        };

        std::vector<const WalkLink*> make_walk_link_order(
            const std::vector<WalkLink>& walk_links
            , bool stable_ordering
        ) {
            std::vector<const WalkLink*> order;
            order.reserve(walk_links.size());
            std::transform(
                walk_links.begin(), walk_links.end(),
                std::back_inserter(order),
                [](const WalkLink& link) { return &link; }
            );
            if (stable_ordering) {
                std::sort(order.begin(), order.end(), [](const WalkLink* a, const WalkLink* b) {
                    return a->id.get() < b->id.get();
                });
            }
            return order;
        }

        mathfp::Expected<double> compute_walk_weight(
            const WalkLink& link
            , const PreprocessParams& params
        ) {
            double w = 0.0;
            switch (params.walk_cost_kind) {
            case WalkCostKind::Time:
                w = link.walk_time.value();
                break;
            case WalkCostKind::Length:
                w = link.length.value();
                break;
            case WalkCostKind::Weighted:
                w = mathfp::units::as_dimless(params.walk_cost.w_time) * link.walk_time.value()
                    + mathfp::units::as_dimless(params.walk_cost.w_length) * link.length.value();
                break;
            }
            if (!std::isfinite(w))
                return mathfp::unexpected(
                    mathfp::domain_error("walk link weight is not finite")
                    .ctx("walk_link_id", link.id.get())
                );
            if (w < 0.0)
                return mathfp::unexpected(
                    mathfp::invalid_arg("walk link weight is negative")
                    .ctx("walk_link_id", link.id.get())
                    .ctx("walk_weight", w)
                );
            return w;
        }

        WalkGraphData init_walk_graph_storage(
            const std::vector<WalkLink>& walk_links
        ) {
            WalkGraphData data;
            data.index_by_key.reserve(walk_links.size() * 2);
            data.best_edges.reserve(walk_links.size());
            data.link_by_id.reserve(walk_links.size());
            return data;
        }

        std::size_t get_or_add_endpoint(
            WalkGraphData& data
            , const WalkEndpoint& endpoint
        ) {
            const auto key = to_endpoint_key(endpoint);
            const auto it = data.index_by_key.find(key);
            if (it != data.index_by_key.end())
                return it->second;
            const auto idx = data.endpoints.size();
            data.endpoints.push_back(endpoint);
            data.index_by_key.emplace(key, idx);
            return idx;
        }

        mathfp::Expected<WalkGraphData> build_walk_graph_data(
            const std::vector<WalkLink>& walk_links
            , const PreprocessParams& params
        ) {
            auto data = init_walk_graph_storage(walk_links);
            const auto link_order = make_walk_link_order(walk_links, params.stable_ordering);

            for (const auto* link_ptr : link_order) {
                const auto& link = *link_ptr;
                data.link_by_id.emplace(link.id, &link);

                MATHFP_TRY_LET(
                    double
                    , w
                    , compute_walk_weight(link, params)
                );

                const auto u = get_or_add_endpoint(data, link.from);
                const auto v = get_or_add_endpoint(data, link.to);

                const Pair key{ u, v };
                const auto it = data.best_edges.find(key);
                if (it == data.best_edges.end() || w < it->second.weight) {
                    data.best_edges[key] = BestEdge{ w, link.id };
                }
            }

            if (params.stable_ordering) {
                // TODO: Avoid rebuilding best_edges from scratch after endpoint sorting.
                // The current implementation favors determinism over efficiency.
                std::sort(data.endpoints.begin(), data.endpoints.end(), [](const WalkEndpoint& a, const WalkEndpoint& b) {
                    return to_endpoint_key(a) < to_endpoint_key(b);
                });
                data.index_by_key.clear();
                data.index_by_key.reserve(data.endpoints.size());
                for (std::size_t i = 0; i < data.endpoints.size(); ++i) {
                    data.index_by_key.emplace(to_endpoint_key(data.endpoints[i]), i);
                }

                data.best_edges.clear();
                data.best_edges.reserve(walk_links.size());
                for (const auto* link_ptr : link_order) {
                    const auto& link = *link_ptr;
                    MATHFP_TRY_LET(
                        double
                        , w
                        , compute_walk_weight(link, params)
                    );

                    const auto u = data.index_by_key.at(to_endpoint_key(link.from));
                    const auto v = data.index_by_key.at(to_endpoint_key(link.to));

                    const Pair key{ u, v };
                    const auto it = data.best_edges.find(key);
                    if (it == data.best_edges.end() || w < it->second.weight) {
                        data.best_edges[key] = BestEdge{ w, link.id };
                    }
                }
            }

            return data;
        }

        mathfp::Expected<std::optional<std::vector<WalkLinkId>>> recover_path(
            const WalkGraphData& data
            , std::size_t s
            , std::size_t t
            , const std::vector<mathfp::graph::VertexId>& parent
        ) {
            std::vector<WalkLinkId> path;
            std::size_t cur = t;
            while (cur != s) {
                const auto pid = parent[cur];
                if (!mathfp::is_valid(pid))
                    return std::optional<std::vector<WalkLinkId>>{};
                const auto p = mathfp::to_usize(pid);
                const Pair key{ p, cur };
                const auto it = data.best_edges.find(key);
                if (it == data.best_edges.end())
                    return mathfp::unexpected(
                        mathfp::internal_error("missing walk edge for shortest path")
                        .ctx("from", static_cast<std::int64_t>(p))
                        .ctx("to", static_cast<std::int64_t>(cur))
                    );
                path.push_back(it->second.id);
                cur = p;
            }
            std::reverse(path.begin(), path.end());
            return std::optional<std::vector<WalkLinkId>>{ std::move(path) };
        }

        std::pair<Length, Time> accumulate_walk_metrics(
            const WalkGraphData& data
            , const std::vector<WalkLinkId>& path
        ) {
            Length total_len{ 0.0 };
            Time total_time{ 0.0 };
            for (const auto& link_id : path) {
                const auto it = data.link_by_id.find(link_id);
                if (it == data.link_by_id.end())
                    continue;
                total_len = Length{ total_len.value() + it->second->length.value() };
                total_time = Time{ total_time.value() + it->second->walk_time.value() };
            }
            return { total_len, total_time };
        }

        struct LineRouteBuildStats final {
            std::size_t skipped_short_routes{};
            std::size_t skipped_missing_trips{};
            std::size_t skipped_stop_times{};
        };

        struct OrderedLineGenerationInputs final {
            std::vector<const Route*> route_order{};
            TripsByRoute              trips_by_route{};
        };

        OrderedLineGenerationInputs ordered_line_generation_inputs(
            const std::vector<Route>& routes
            , const std::vector<Trip>& trips
            , bool stable_ordering
        ) {
            // Deterministic RouteSegmentId assignment is defined here:
            // routes are generated in ordered_routes(...) order,
            // and route-local trip data is aggregated from ordered_route_trips(...).
            return OrderedLineGenerationInputs{
                .route_order = ordered_routes(routes, stable_ordering),
                .trips_by_route = ordered_route_trips(
                    group_trips_by_route(trips),
                    stable_ordering
                )
            };
        }

        mathfp::Expected<const std::vector<const Trip*>*> find_route_trips(
            const Route& route
            , const TripsByRoute& trips_by_route
            , LineRouteBuildStats& stats
        ) {
            const auto it = trips_by_route.find(route.id);
            if (it != trips_by_route.end()) {
                return &it->second;
            }

            ++stats.skipped_missing_trips;
            return mathfp::unexpected(
                mathfp::invalid_arg("route has no trips")
                .ctx("route_id", route.id.get())
            );
        }

        mathfp::Expected<std::vector<RouteSegment>> build_pairwise_line_route_segments(
            const Route& route
            , const std::vector<Time>& cumulative_time
            , const std::vector<Length>& cumulative_length
            , std::int64_t& next_id
        ) {
            const auto& route_stops = route.stops;
            std::vector<RouteSegment> segments;
            segments.reserve(route_stops.size() * (route_stops.size() - 1) / 2);

            for (std::size_t i = 0; i < route_stops.size(); ++i) {
                for (std::size_t j = i + 1; j < route_stops.size(); ++j) {
                    const auto run_time = Time{
                        cumulative_time[j].value() - cumulative_time[i].value()
                    };
                    const auto length = Length{
                        cumulative_length[j].value() - cumulative_length[i].value()
                    };
                    MATHFP_TRY_LET(
                        RouteSegment
                        , segment
                        , make_route_segment(
                            RouteSegmentId{ next_id++ }
                            , WalkEndpoint{ route_stops[i] }
                            , WalkEndpoint{ route_stops[j] }
                            , length
                            , run_time
                            , SegmentCarrier{ route.line }
                        )
                    );
                    segments.push_back(std::move(segment));
                }
            }

            return segments;
        }

        mathfp::Expected<std::vector<RouteSegment>> build_line_route_segments_for_route(
            const Route& route
            , const TripsByRoute& trips_by_route
            , const PreprocessParams& params
            , std::int64_t& next_id
            , LineRouteBuildStats& stats
        ) {
            if (route.stops.size() < 2) {
                ++stats.skipped_short_routes;
                return std::vector<RouteSegment>{};
            }

            MATHFP_TRY_LET(
                const std::vector<const Trip*>*
                , route_trips
                , find_route_trips(route, trips_by_route, stats)
            );
            MATHFP_TRY_LET(
                StopPairDurations
                , durations
                , collect_durations(route, *route_trips)
            );

            auto edge_metrics = build_edge_metrics(route, durations, params);
            if (!edge_metrics) {
                if (params.strict_stop_times) {
                    return mathfp::unexpected(edge_metrics.error());
                }
                ++stats.skipped_stop_times;
                return std::vector<RouteSegment>{};
            }

            const auto& edge_time = edge_metrics->first;
            const auto& edge_len = edge_metrics->second;
            const auto [cum_time, cum_len] = build_cumulative_metrics(edge_time, edge_len);

            return build_pairwise_line_route_segments(
                route, cum_time, cum_len, next_id
            );
        }

        void log_line_route_segment_stats(
            const std::vector<RouteSegment>& segments
            , const LineRouteBuildStats& stats
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;

            log(
                fmt::format(
                    "line segments: total = {:>8}  skipped_short_routes = {:>6}  skipped_missing_trips = {:>6}  skipped_stop_times = {:>6}"
                    , segments.size()
                    , stats.skipped_short_routes
                    , stats.skipped_missing_trips
                    , stats.skipped_stop_times
                ),
                LogLevel::Info
            );
        }

    }  // namespace

    mathfp::Expected<std::vector<RouteSegment>> build_line_route_segments(
        const std::vector<Route>& routes
        , const std::vector<Trip>& trips
        , const std::vector<Stop>& stops
        , const PreprocessParams& params
    ) {
        // TODO: Either use stops for explicit route/stop validation here or drop the
        // parameter from this path once the intended contract is finalized.
        (void)stops;
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: line route segments");
        log(
            fmt::format(
                "line segments input: routes = {:>6}  trips = {:>6}  stops = {:>6}"
                , routes.size()
                , trips.size()
                , stops.size()
            )
            , LogLevel::Info
        );

        std::vector<RouteSegment> out;
        std::int64_t next_id = 0;
        const auto ordered_inputs = ordered_line_generation_inputs(
            routes, trips, params.stable_ordering
        );
        // TODO: Derive a better reserve estimate for out from route stop counts if
        // line-segment preprocessing becomes a hotspot on large inputs.
        log(
            fmt::format(
                "line segments params: stable_ordering = {}  strict_stop_times = {}  time_aggregation = {}"
                , params.stable_ordering   ? "true" : "false"
                , params.strict_stop_times ? "true" : "false"
                , static_cast<int>(params.time_aggregation)
            ),
            LogLevel::Info
        );

        LineRouteBuildStats stats;

        for (const auto* route : ordered_inputs.route_order) {
            MATHFP_TRY_LET(
                std::vector<RouteSegment>
                , route_segments
                , build_line_route_segments_for_route(
                    *route
                    , ordered_inputs.trips_by_route
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
        }

        log_line_route_segment_stats(out, stats);
        both("preprocessing: line route segments done");

        return out;
    }

    mathfp::Expected<std::vector<RouteSegment>> build_walk_route_segments(
        const std::vector<WalkLink>& walk_links
        , const PreprocessParams& params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("preprocessing: walk route segments");
        if (walk_links.empty())
            return std::vector<RouteSegment>{};

        std::vector<WalkKey> seen_keys;

        MATHFP_TRY_LET(
            WalkGraphData
            , data
            , build_walk_graph_data(walk_links, params)
        );
        log(
            fmt::format(
                "walk segments input: walk_links = {:>6}  endpoints = {:>6}  edges = {:>6}"
                , walk_links.size()
                , data.endpoints.size()
                , data.best_edges.size()
            ),
            LogLevel::Info
        );
        log(
            fmt::format(
                "walk segments params: stable_ordering = {}  deduplicate = {}  cost_kind = {}"
                , params.stable_ordering           ? "true" : "false"
                , params.deduplicate_walk_segments ? "true" : "false"
                , static_cast<int>(params.walk_cost_kind)
            ),
            LogLevel::Info
        );

        mathfp::graph::DiGraph<double> g(data.endpoints.size());
        for (const auto& [pair, edge] : data.best_edges) {
            boost::add_edge(pair.first, pair.second, edge.weight, g);
        }

        std::vector<RouteSegment> out;
        std::int64_t next_id = 0;
        if (params.deduplicate_walk_segments) {
            seen_keys.reserve(data.endpoints.size() * data.endpoints.size());
        }

        std::size_t skipped_unreachable = 0;
        std::size_t skipped_duplicate   = 0;

        using WalkGraph = mathfp::graph::DiGraph<double>;
        using DijkstraResult = mathfp::graph::DijkstraResult<WalkGraph>;

        for (std::size_t s = 0; s < data.endpoints.size(); ++s) {
            const auto start = mathfp::Index<mathfp::graph::VertexIdTag>(s);
            MATHFP_TRY_LET(
                DijkstraResult
                , res
                , mathfp::graph::dijkstra(g, start)
            );

            const auto& dist   = res.distance;
            const auto& parent = res.parent;

            for (std::size_t t = 0; t < data.endpoints.size(); ++t) {
                if (t == s)
                    continue;
                const auto d = dist[t];
                if (!std::isfinite(d)) {
                    ++skipped_unreachable;
                    continue;
                }
                MATHFP_TRY_LET(
                    std::optional<std::vector<WalkLinkId>>
                    , maybe_path
                    , recover_path(data, s, t, parent)
                );
                if (!maybe_path.has_value())
                    continue;

                auto path_vec = std::move(*maybe_path);
                const auto [total_len, total_time] = accumulate_walk_metrics(data, path_vec);
                WalkKey key{
                    to_endpoint_key(data.endpoints[s])
                    , to_endpoint_key(data.endpoints[t])
                    , path_vec
                };

                if (params.deduplicate_walk_segments) {
                    auto it = std::lower_bound(seen_keys.begin(), seen_keys.end(), key);
                    if (it != seen_keys.end() && *it == key) {
                        ++skipped_duplicate;
                        continue;
                    }
                    seen_keys.insert(it, std::move(key));
                }

                MATHFP_TRY_LET(
                    RouteSegment
                    , segment
                    , make_route_segment(
                        RouteSegmentId{ next_id++ }
                        , data.endpoints[s]
                        , data.endpoints[t]
                        , total_len
                        , total_time
                        , SegmentCarrier{ std::move(path_vec) }
                    )
                );
                out.push_back(std::move(segment));
            }
        }

        log(
            fmt::format(
                "walk segments: total = {:>8}  skipped_unreachable = {:>8}  skipped_duplicate = {:>8}"
                , out.size()
                , skipped_unreachable
                , skipped_duplicate
            ),
            LogLevel::Info
        );
        both("preprocessing: walk route segments done");

        return out;
    }

}  // namespace timetable::domain::preprocessing
