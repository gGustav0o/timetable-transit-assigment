#pragma once

#include <cmath>
#include <string_view>
#include <type_traits>
#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/segment_semantics.hpp"
#include "timetable/domain/segments.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain::preprocessing {

    namespace detail {

        inline mathfp::Expected<mathfp::Unit> ensure_walk_path_nonempty(
            const WalkPath& path
        ) {
            if (path.empty()) {
                const char* message = "walk path must be non-empty";
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            return mathfp::kUnit;
        }

        inline bool same_walk_endpoint(const WalkEndpoint& a, const WalkEndpoint& b) {
            if (a.index() != b.index()) {
                return false;
            }

            if (std::holds_alternative<StopId>(a)) {
                return std::get<StopId>(a) == std::get<StopId>(b);
            }

            return std::get<ZoneId>(a) == std::get<ZoneId>(b);
        }

        inline mathfp::Expected<mathfp::Unit> ensure_distinct_walk_endpoints(
              const WalkEndpoint&   from
            , const WalkEndpoint& to
        ) {
            if (!same_walk_endpoint(from, to)) {
                return mathfp::kUnit;
            }

            const char* message = "walk endpoints must be distinct";
            return validation::fail(message, mathfp::invalid_arg(message));
        }

        inline mathfp::Expected<mathfp::Unit> ensure_route_position_nonnegative(
              RoutePosition position
            , const char* name
        ) {
            if (position.get() < 0) {
                const char* message = "route position must be non-negative";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message).ctx("name", name).ctx("position", position.get())
                );
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_occurrence_positions_strictly_ordered(
              const StopOccurrence&   from
            , const StopOccurrence& to
        ) {
            if (to.position > from.position) {
                return mathfp::kUnit;
            }

            const char* message = "line route topology must progress strictly in route position";
            return validation::fail(
                  message
                , mathfp::invalid_arg(message)
                    .ctx("from_stop_id" , from.stop.get())
                    .ctx("from_position", from.position.get())
                    .ctx("to_stop_id"   , to.stop.get())
                    .ctx("to_position"  , to.position.get())
            );
        }

        inline mathfp::Expected<mathfp::Unit> ensure_walk_topology_consistent(
            const WalkRouteTopology& topology
        ) {
            MATHFP_TRY(ensure_distinct_walk_endpoints(topology.from, topology.to));
            MATHFP_TRY(ensure_walk_path_nonempty(topology.path));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_line_topology_consistent(
            const LineRouteTopology& topology
        ) {
            MATHFP_TRY(ensure_route_position_nonnegative(topology.from.position, "from.position"));
            MATHFP_TRY(ensure_route_position_nonnegative(topology.to.position, "to.position"));
            MATHFP_TRY(ensure_occurrence_positions_strictly_ordered(topology.from, topology.to));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_route_topology_consistent(
            const RouteTopology& topology
        ) {
            return std::visit(
                []<class T>(const T& value) -> mathfp::Expected<mathfp::Unit> {
                    if constexpr (std::is_same_v<T, WalkRouteTopology>) {
                        return ensure_walk_topology_consistent(value);
                    } else {
                        return ensure_line_topology_consistent(value);
                    }
                }
                , topology
            );
        }

        inline mathfp::Expected<mathfp::Unit> ensure_time_pair_consistent(
              const std::optional<Time>&   dep
            , const std::optional<Time>& arr
        ) {
            const auto has_dep = dep.has_value();
            const auto has_arr = arr.has_value();
            if (has_dep != has_arr) {
                const char* message = "departure and arrival must both be set or both be empty";
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            if (!has_dep) {
                return mathfp::kUnit;
            }

            MATHFP_TRY(validation::ensure_nonneg(*dep, "departure"));
            MATHFP_TRY(validation::ensure_nonneg(*arr, "arrival"));

            if (arr->value() < dep->value()) {
                const char* message = "arrival must be >= departure";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message)
                        .ctx("departure", dep->value())
                        .ctx("arrival"  , arr->value())
                );
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_fare_consistent(
            const std::optional<double>& fare
        ) {
            if (!fare) {
                return mathfp::kUnit;
            }

            if (!std::isfinite(*fare)) {
                const char* message = "fare is not finite";
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            if (*fare < 0.0) {
                const char* message = "fare must be non-negative";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message).ctx("fare", *fare)
                );
            }
            return mathfp::kUnit;
        }

        template <class T>
        inline mathfp::Expected<mathfp::Unit> ensure_present(
              const std::optional<T>& value
            , std::string_view      message
        ) {
            if (!value.has_value()) {
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            return mathfp::kUnit;
        }

        template <class T>
        inline mathfp::Expected<mathfp::Unit> ensure_absent(
              const std::optional<T>& value
            , std::string_view      message
        ) {
            if (value.has_value()) {
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_index_pair_presence_consistent(
              const std::optional<RoutePosition>&   from_index
            , const std::optional<RoutePosition>& to_index
        ) {
            if (from_index.has_value() != to_index.has_value()) {
                const char* message = "from_index and to_index must both be set or both be empty";
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_walk_segment_has_no_trip_metadata(
              const std::optional<TripId>&          trip
            , const std::optional<RoutePosition>& from_index
            , const std::optional<RoutePosition>& to_index
        ) {
            const char* message = "walk segment must not carry trip metadata";

            MATHFP_TRY(ensure_absent(trip      , message));
            MATHFP_TRY(ensure_absent(from_index, message));
            MATHFP_TRY(ensure_absent(to_index  , message));

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_line_segment_has_trip(
            const std::optional<TripId>& trip
        ) {
            return ensure_present(trip, "line segment must carry trip metadata");
        }

        inline mathfp::Expected<mathfp::Unit> ensure_line_segment_has_indices(
              const std::optional<RoutePosition>&   from_index
            , const std::optional<RoutePosition>& to_index
        ) {
            const char* message = "line segment must carry route positions";

            MATHFP_TRY(ensure_present(from_index, message));
            MATHFP_TRY(ensure_present(to_index  , message));

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_trip_id_nonnegative(
            const TripId& trip
        ) {
            if (trip.get() < 0) {
                const char* message = "trip_id must be non-negative";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message).ctx("trip_id", trip.get())
                );
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_indices_nonnegative(
              RoutePosition   from_index
            , RoutePosition to_index
        ) {
            MATHFP_TRY(ensure_route_position_nonnegative(from_index, "from_index"));
            MATHFP_TRY(ensure_route_position_nonnegative(to_index  , "to_index"));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_indices_strictly_ordered(
              RoutePosition   from_index
            , RoutePosition to_index
        ) {
            if (to_index > from_index) {
                return mathfp::kUnit;
            }

            const char* message = "to_index must be greater than from_index";
            return validation::fail(
                  message
                , mathfp::invalid_arg(message)
                    .ctx("from_index", from_index.get())
                    .ctx("to_index"  , to_index.get())
            );
        }

        inline mathfp::Expected<mathfp::Unit> ensure_connection_route_positions_match(
              const RouteSegment& route_segment
            , RoutePosition     from_index
            , RoutePosition     to_index
        ) {
            if (const auto* line = line_topology_of(route_segment); line != nullptr) {
                if (line->from.position != from_index || line->to.position != to_index) {
                    const char* message = "connection segment route positions must match route topology";
                    return validation::fail(
                          message
                        , mathfp::invalid_arg(message)
                            .ctx("route_segment_id"   , route_segment.id.get())
                            .ctx("expected_from_index", line->from.position.get())
                            .ctx("actual_from_index"  , from_index.get())
                            .ctx("expected_to_index"  , line->to.position.get())
                            .ctx("actual_to_index"    , to_index.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_trip_metadata_consistent(
              const RouteSegment&                   route_segment
            , const std::optional<TripId>&        trip
            , const std::optional<RoutePosition>& from_index
            , const std::optional<RoutePosition>& to_index
        ) {
            MATHFP_TRY(ensure_index_pair_presence_consistent(from_index, to_index));

            if (is_walk(route_segment)) {
                MATHFP_TRY(ensure_walk_segment_has_no_trip_metadata(trip, from_index, to_index));
                return mathfp::kUnit;
            }

            MATHFP_TRY(ensure_line_segment_has_trip(trip));
            MATHFP_TRY(ensure_line_segment_has_indices(from_index, to_index));
            MATHFP_TRY(ensure_trip_id_nonnegative(*trip));
            MATHFP_TRY(ensure_indices_nonnegative(from_index.value(), to_index.value()));
            MATHFP_TRY(ensure_indices_strictly_ordered(from_index.value(), to_index.value()));
            MATHFP_TRY(ensure_connection_route_positions_match(
                  route_segment
                , from_index.value()
                , to_index.value()
            ));

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_route_timing_consistent(
              const RouteSegment& route_segment
            , bool              has_times
        ) {
            if (is_line(route_segment) && !has_times) {
                const char* message = "line segment must have times";
                return validation::fail(message, mathfp::invalid_arg(message));
            }
            if (is_walk(route_segment) && has_times) {
                const char* message = "walk segment must not have times";
                return validation::fail(message, mathfp::invalid_arg(message));
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_connection_segment_invariants(
              const RouteSegment&                   route_segment
            , const std::optional<TripId>&        trip
            , const std::optional<RoutePosition>& from_index
            , const std::optional<RoutePosition>& to_index
            , const std::optional<Time>&          departure
            , const std::optional<Time>&          arrival
            , const std::optional<double>&        fare
        ) {
            MATHFP_TRY(ensure_time_pair_consistent(departure, arrival));
            MATHFP_TRY(ensure_fare_consistent(fare));
            MATHFP_TRY(ensure_trip_metadata_consistent(
                  route_segment
                , trip
                , from_index
                , to_index
            ));
            MATHFP_TRY(ensure_route_timing_consistent(
                  route_segment
                , departure.has_value()
            ));
            return mathfp::kUnit;
        }

    }  // namespace detail

    inline mathfp::Expected<RouteSegment> make_route_segment(
          RouteSegmentId  id
        , Length        length
        , Time          run_time
        , RouteTopology topology
    ) {
        MATHFP_TRY(validation::ensure_nonneg(length, "length"));
        MATHFP_TRY(validation::ensure_nonneg(run_time, "run_time"));
        MATHFP_TRY(detail::ensure_route_topology_consistent(topology));

        return RouteSegment{
              .id         = id
            , .length   = length
            , .run_time = run_time
            , .topology = std::move(topology)
        };
    }

    inline mathfp::Expected<RouteSegment> make_route_segment(
          RouteSegmentId id
        , WalkEndpoint from
        , WalkEndpoint to
        , Length       length
        , Time         run_time
        , WalkPath     path
    ) {
        return make_route_segment(
              id
            , length
            , run_time
            , RouteTopology{
                WalkRouteTopology{
                      .from   = std::move(from)
                    , .to   = std::move(to)
                    , .path = std::move(path)
                }
            }
        );
    }

    inline mathfp::Expected<RouteSegment> make_route_segment(
          RouteSegmentId   id
        , StopOccurrence from
        , StopOccurrence to
        , Length         length
        , Time           run_time
        , LineId         line
    ) {
        return make_route_segment(
              id
            , length
            , run_time
            , RouteTopology{
                LineRouteTopology{
                      .from   = std::move(from)
                    , .to   = std::move(to)
                    , .line = line
                }
            }
        );
    }

    inline mathfp::Expected<ConnectionSegment> make_connection_segment(
          ConnectionSegmentId            id
        , const RouteSegment&          route_segment
        , std::optional<TripId>        trip
        , std::optional<RoutePosition> from_index
        , std::optional<RoutePosition> to_index
        , std::optional<Time>          departure
        , std::optional<Time>          arrival
        , std::optional<double>        fare
    ) {
        MATHFP_TRY(detail::ensure_connection_segment_invariants(
              route_segment
            , trip
            , from_index
            , to_index
            , departure
            , arrival
            , fare
        ));

        return ConnectionSegment{
              .id              = id
            , .route_segment = route_segment.id
            , .trip          = std::move(trip)
            , .from_index    = std::move(from_index)
            , .to_index      = std::move(to_index)
            , .departure     = std::move(departure)
            , .arrival       = std::move(arrival)
            , .fare          = std::move(fare)
        };
    }

}  // namespace timetable::domain::preprocessing
