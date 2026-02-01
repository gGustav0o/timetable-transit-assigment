#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/segments.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain::preprocessing {

    namespace detail {

        inline mathfp::Expected<mathfp::Unit> ensure_walk_path_nonempty(
            const WalkPath& path
        ) {
            if (path.empty())
                return validation::fail(
                    "walk path must be non-empty"
                    , mathfp::invalid_arg("walk path must be non-empty")
                );
            return mathfp::kUnit;
        }

        inline bool same_endpoint(const WalkEndpoint& a, const WalkEndpoint& b) {
            if (a.index() != b.index())
                return false;
            if (std::holds_alternative<StopId>(a))
                return std::get<StopId>(a) == std::get<StopId>(b);
            return std::get<ZoneId>(a) == std::get<ZoneId>(b);
        }

        inline mathfp::Expected<mathfp::Unit> ensure_distinct_endpoints(
            const WalkEndpoint& from
            , const WalkEndpoint& to
        ) {
            if (same_endpoint(from, to))
                return validation::fail(
                    "from and to must be distinct"
                    , mathfp::invalid_arg("from and to must be distinct")
                );
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_stop_endpoint(
            const WalkEndpoint& endpoint
            , const char* name
        ) {
            if (!std::holds_alternative<StopId>(endpoint))
                return validation::fail(
                    "endpoint must be a stop"
                    , mathfp::invalid_arg("endpoint must be a stop").ctx("name", name)
                );
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_time_pair_consistent(
            const std::optional<Time>& dep
            , const std::optional<Time>& arr
        ) {
            const auto has_dep = dep.has_value();
            const auto has_arr = arr.has_value();
            if (has_dep != has_arr)
                return validation::fail(
                    "departure and arrival must both be set or both be empty"
                    , mathfp::invalid_arg("departure and arrival must both be set or both be empty")
                );

            if (!has_dep)
                return mathfp::kUnit;

            if (auto r = validation::ensure_nonneg(*dep, "departure"); !r)
                return mathfp::unexpected(r.error());
            if (auto r = validation::ensure_nonneg(*arr, "arrival"); !r)
                return mathfp::unexpected(r.error());

            if (arr->value() < dep->value())
                return validation::fail(
                    "arrival must be >= departure"
                    , mathfp::invalid_arg("arrival must be >= departure")
                        .ctx("departure", dep->value())
                        .ctx("arrival", arr->value())
                );

            return mathfp::kUnit;
        }

    }  // namespace detail

    inline mathfp::Expected<RouteSegment> make_route_segment(
        RouteSegmentId id
        , WalkEndpoint from
        , WalkEndpoint to
        , Length length
        , Time run_time
        , SegmentCarrier carrier
    ) {
        if (auto r = validation::ensure_nonneg(length, "length"); !r)
            return mathfp::unexpected(r.error());
        if (auto r = validation::ensure_nonneg(run_time, "run_time"); !r)
            return mathfp::unexpected(r.error());
        if (auto r = detail::ensure_distinct_endpoints(from, to); !r)
            return mathfp::unexpected(r.error());

        if (std::holds_alternative<LineId>(carrier)) {
            if (auto r = detail::ensure_stop_endpoint(from, "from"); !r)
                return mathfp::unexpected(r.error());
            if (auto r = detail::ensure_stop_endpoint(to, "to"); !r)
                return mathfp::unexpected(r.error());
        } else {
            const auto& path = std::get<WalkPath>(carrier);
            if (auto r = detail::ensure_walk_path_nonempty(path); !r)
                return mathfp::unexpected(r.error());
        }

        return RouteSegment{
            .id = id,
            .from = std::move(from),
            .to = std::move(to),
            .length = length,
            .run_time = run_time,
            .carrier = std::move(carrier)
        };
    }

    inline mathfp::Expected<ConnectionSegment> make_connection_segment(
        ConnectionSegmentId id
        , const RouteSegment& route_segment
        , std::optional<Time> departure
        , std::optional<Time> arrival
    ) {
        if (auto r = detail::ensure_time_pair_consistent(departure, arrival); !r)
            return mathfp::unexpected(r.error());

        const auto is_line = std::holds_alternative<LineId>(route_segment.carrier);
        const auto has_times = departure.has_value();
        if (is_line && !has_times)
            return validation::fail(
                "line segment must have times"
                , mathfp::invalid_arg("line segment must have times")
            );
        if (!is_line && has_times)
            return validation::fail(
                "walk segment must not have times"
                , mathfp::invalid_arg("walk segment must not have times")
            );

        return ConnectionSegment{
            .id = id,
            .route_segment = route_segment.id,
            .departure = std::move(departure),
            .arrival = std::move(arrival)
        };
    }

}  // namespace timetable::domain::preprocessing
