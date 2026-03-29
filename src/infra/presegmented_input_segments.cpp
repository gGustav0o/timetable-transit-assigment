#include "detail/presegmented_input.hpp"

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/numeric.hpp"
#include "timetable/domain/preprocessing/segments_factory.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::detail::presegmented_input {
    namespace {

        [[nodiscard]] bool line_route_metrics_equal(
              const LineRouteMetrics& lhs
            , const LineRouteMetrics& rhs
        ) {
            return mathfp::almost_equal(lhs.length_km, rhs.length_km)
                && mathfp::almost_equal(lhs.time_sec, rhs.time_sec);
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> report_build_progress(
          std::size_t       index
        , std::size_t       total
        , const BuildStats& stats
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        if (index == 0 || (index % kProgressStep) != 0) {
            return mathfp::kUnit;
        }

        const auto pct = static_cast<int>(
            100.0 * static_cast<double>(index) / static_cast<double>(total)
        );
        status(fmt::format("parsing: building segments ({}%)", pct));
        log(
            fmt::format(
                  "parsing: progress i={}  walk={}  line={}  timed={}  untimed={}"
                , index
                , stats.walk_segments
                , stats.line_segments
                , stats.timed_segments
                , stats.untimed_segments
            )
            , LogLevel::Debug
        );
        return mathfp::kUnit;
    }

    mathfp::Expected<std::size_t> build_route_segment_for_row(
          BuildState&             state
        , const SegmentRowView&   row
        , const SegmentSemantics& semantics
    ) {
        using timetable::domain::Length;
        using timetable::domain::LineId;
        using timetable::domain::RouteSegment;
        using timetable::domain::RouteSegmentId;
        using timetable::domain::Time;
        using timetable::domain::WalkLinkId;
        using timetable::domain::WalkPath;
        using timetable::domain::preprocessing::make_route_segment;

        if (semantics.is_walk_segment) {
            MATHFP_TRY_LET(
                  RouteSegment
                , route
                , make_route_segment(
                      RouteSegmentId{ state.next_route_segment_id++ }
                    , semantics.from_endpoint
                    , semantics.to_endpoint
                    , Length{ row.length_km }
                    , Time{ row.time_sec }
                    , WalkPath{ WalkLinkId{ state.next_walk_id++ } }
                )
            );
            state.route_segments.push_back(std::move(route));
            return state.route_segments.size() - 1;
        }

        const LineRouteKey key{
              .from    = timetable::domain::occurrence_key(*semantics.from_occurrence)
            , .to      = timetable::domain::occurrence_key(*semantics.to_occurrence)
            , .line_id = row.profile
        };

        if (const auto it = state.line_routes.find(key); it != state.line_routes.end()) {
            const auto actual_metrics = LineRouteMetrics{
                  .length_km = row.length_km
                , .time_sec  = row.time_sec
            };
            if (!line_route_metrics_equal(it->second.metrics, actual_metrics)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("inconsistent TIME/LENGTH for identical line occurrence pair")
                        .ctx(std::string(kCtxIndex)     , static_cast<std::int64_t>(row.index))
                        .ctx(std::string(kCtxLineId)    , row.profile)
                        .ctx(std::string(kCtxFromStopId), semantics.from_occurrence->stop.get())
                        .ctx(std::string(kCtxFromIndex) , semantics.from_occurrence->position.get())
                        .ctx(std::string(kCtxToStopId)  , semantics.to_occurrence->stop.get())
                        .ctx(std::string(kCtxToIndex)   , semantics.to_occurrence->position.get())
                        .ctx("expected_length"          , it->second.metrics.length_km)
                        .ctx("actual_length"            , row.length_km)
                        .ctx("expected_time"            , it->second.metrics.time_sec)
                        .ctx("actual_time"              , row.time_sec)
                );
            }

            const auto route_index = it->second.route_segment_index;
            if (route_index >= state.route_segments.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("missing route segment for deduplicated line route key")
                        .ctx(std::string(kCtxIndex) , static_cast<std::int64_t>(row.index))
                        .ctx(std::string(kCtxLineId), row.profile)
                        .ctx("route_segment_index"  , static_cast<std::int64_t>(route_index))
                        .ctx("route_segments_size"  , static_cast<std::int64_t>(state.route_segments.size()))
                );
            }
            return route_index;
        }

        MATHFP_TRY_LET(
              RouteSegment
            , route
            , make_route_segment(
                  RouteSegmentId{ state.next_route_segment_id++ }
                , *semantics.from_occurrence
                , *semantics.to_occurrence
                , Length{ row.length_km }
                , Time{ row.time_sec }
                , LineId{ row.profile }
            )
        );
        state.route_segments.push_back(std::move(route));
        const auto route_segment_index = state.route_segments.size() - 1;
        state.line_routes.emplace(
              key
            , LineRouteEntry{
                  .route_segment_index = route_segment_index
                , .metrics             = LineRouteMetrics{
                      .length_km           = row.length_km
                    , .time_sec            = row.time_sec
                }
            }
        );
        return route_segment_index;
    }

    mathfp::Expected<mathfp::Unit> append_connection_segment_for_row(
          BuildState&             state
        , const SegmentRowView&   row
        , const SegmentSemantics& semantics
        , std::size_t             route_segment_index
    ) {
        using timetable::domain::ConnectionSegment;
        using timetable::domain::ConnectionSegmentId;
        using timetable::domain::RoutePosition;
        using timetable::domain::TripId;
        using timetable::domain::preprocessing::make_connection_segment;

        if (route_segment_index >= state.route_segments.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("missing route segment while appending connection segment")
                    .ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
                    .ctx("route_segment_index" , static_cast<std::int64_t>(route_segment_index))
                    .ctx("route_segments_size" , static_cast<std::int64_t>(state.route_segments.size()))
            );
        }
        const auto& route_segment = state.route_segments[route_segment_index];

        MATHFP_TRY_LET(
              ConnectionSegment
            , conn
            , make_connection_segment(
                  ConnectionSegmentId{ state.next_connection_segment_id++ }
                , route_segment
                , semantics.is_walk_segment ? std::optional<TripId>        {} : std::optional<TripId>        { TripId{ row.trip_id } }
                , semantics.is_walk_segment ? std::optional<RoutePosition> {} : std::optional<RoutePosition> { RoutePosition{ row.from_index } }
                , semantics.is_walk_segment ? std::optional<RoutePosition> {} : std::optional<RoutePosition> { RoutePosition{ row.to_index } }
                , semantics.dep
                , semantics.arr
                , semantics.fare
            )
        );
        state.connection_segments.push_back(std::move(conn));

        if (semantics.is_walk_segment) {
            ++state.stats.walk_segments;
            ++state.stats.untimed_segments;
        } else {
            ++state.stats.line_segments;
            ++state.stats.timed_segments;
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<BuildState> process_segment_row_with_progress(
          BuildState     state
        , SegmentRowView row
        , std::size_t    count
    ) {
        MATHFP_TRY(report_build_progress(row.index, count, state.stats));
        return process_segment_row(std::move(state), std::move(row));
    }

}  // namespace timetable::infra::detail::presegmented_input
