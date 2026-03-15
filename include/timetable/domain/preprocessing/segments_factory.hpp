#pragma once

#include <cmath>
#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

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
            if (same_endpoint(from, to)) {
                const char* message = "from and to must be distinct";
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_stop_endpoint(
            const WalkEndpoint& endpoint
            , const char* name
        ) {
            if (!std::holds_alternative<StopId>(endpoint)) {
				const char* message = "endpoint must be a stop";
                return validation::fail(
                    message
                    , mathfp::invalid_arg(message).ctx("name", name)
                );
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_line_endpoints_are_stops(
            const WalkEndpoint& from
            , const WalkEndpoint& to
        ) {
            MATHFP_TRY(ensure_stop_endpoint(from, "from"));
            MATHFP_TRY(ensure_stop_endpoint(to  , "to"));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_time_pair_consistent(
            const std::optional<Time>& dep
            , const std::optional<Time>& arr
        ) {
            const auto has_dep = dep.has_value();
            const auto has_arr = arr.has_value();
            if (has_dep != has_arr) {
				const char* message = "departure and arrival must both be set or both be empty";
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            if (!has_dep)
                return mathfp::kUnit;

            MATHFP_TRY(validation::ensure_nonneg(*dep, "departure"));
            MATHFP_TRY(validation::ensure_nonneg(*arr, "arrival"));

            if (arr->value() < dep->value()) {
				const char* message = "arrival must be >= departure";
                return validation::fail(
                    message
                    , mathfp::invalid_arg(message)
                        .ctx("departure", dep->value())
                        .ctx("arrival", arr->value())
                );
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_fare_consistent(
            const std::optional<double>& fare
        ) {
            if (!fare) return mathfp::kUnit;

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
            , std::string_view message
        ) {
            if (!value.has_value())
                return validation::fail(
                    message.data()
                    , mathfp::invalid_arg(message)
                );

            return mathfp::kUnit;
        }

        template <class T>
        inline mathfp::Expected<mathfp::Unit> ensure_absent(
            const std::optional<T>& value
            , std::string_view message
        ) {
            if (value.has_value())
                return validation::fail(
                    message.data()
                    , mathfp::invalid_arg(message)
                );

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_index_pair_presence_consistent(
            const std::optional<std::int64_t>& from_index
            , const std::optional<std::int64_t>& to_index
        ) {
            if (from_index.has_value() != to_index.has_value()) {
				const char* message = "from_index and to_index must both be set or both be empty";
                return validation::fail(message, mathfp::invalid_arg(message));
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_walk_segment_has_no_trip_metadata(
            const std::optional<TripId>& trip
            , const std::optional<std::int64_t>& from_index
            , const std::optional<std::int64_t>& to_index
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
            return ensure_present(
                trip, "line segment must carry trip metadata"
            );
        }

        inline mathfp::Expected<mathfp::Unit> ensure_line_segment_has_indices(
            const std::optional<std::int64_t>& from_index
            , const std::optional<std::int64_t>& to_index
        ) {
			const char* message = "line segment must carry stop indices";

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
            std::int64_t from_index
            , std::int64_t to_index
        ) {
            if (from_index < 0 || to_index < 0) {
				const char* message = "stop indices must be non-negative";
                return validation::fail(
                    message
                    , mathfp::invalid_arg(message)
                        .ctx("from_index", from_index)
                        .ctx("to_index", to_index)
                );
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_indices_strictly_ordered(
            std::int64_t from_index
            , std::int64_t to_index
        ) {
            if (to_index <= from_index) {
				const char* message = "to_index must be greater than from_index";
                return validation::fail(
                    message
                    , mathfp::invalid_arg(message)
                        .ctx("from_index", from_index)
                        .ctx("to_index"  , to_index)
                );
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_trip_metadata_consistent(
            bool is_line
            , const std::optional<TripId>& trip
            , const std::optional<std::int64_t>& from_index
            , const std::optional<std::int64_t>& to_index
        ) {
            MATHFP_TRY(ensure_index_pair_presence_consistent(
                from_index, to_index
            ));

            if (!is_line) {
                MATHFP_TRY(ensure_walk_segment_has_no_trip_metadata(
                    trip, from_index, to_index
                ));
                return mathfp::kUnit;
            }

            MATHFP_TRY(ensure_line_segment_has_trip(trip));
            MATHFP_TRY(ensure_line_segment_has_indices(from_index, to_index));
            MATHFP_TRY(ensure_trip_id_nonnegative(*trip));
            MATHFP_TRY(ensure_indices_nonnegative(
                from_index.value(), to_index.value()
            ));
            MATHFP_TRY(ensure_indices_strictly_ordered(
                from_index.value(), to_index.value()
            ));

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_carrier_timing_consistent(
            bool is_line
            , bool has_times
        ) {
            if (is_line && !has_times) {
				const char* message = "line segment must have times";
                return validation::fail(message, mathfp::invalid_arg(message));
            }
            if (!is_line && has_times) {
				const char* message = "walk segment must not have times";
                return validation::fail(message, mathfp::invalid_arg(message));
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_route_carrier_consistent(
            const WalkEndpoint& from
            , const WalkEndpoint& to
            , const SegmentCarrier& carrier
        ) {
            if (std::holds_alternative<LineId>(carrier)) {
                MATHFP_TRY(ensure_line_endpoints_are_stops(from, to));
                return mathfp::kUnit;
            }

            MATHFP_TRY(ensure_walk_path_nonempty(std::get<WalkPath>(carrier)));

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_connection_segment_invariants(
            const RouteSegment& route_segment
            , const std::optional<TripId>& trip
            , const std::optional<std::int64_t>& from_index
            , const std::optional<std::int64_t>& to_index
            , const std::optional<Time>& departure
            , const std::optional<Time>& arrival
            , const std::optional<double>& fare
        ) {
            MATHFP_TRY(ensure_time_pair_consistent(departure, arrival));
            MATHFP_TRY(ensure_fare_consistent(fare));

            const auto is_line = std::holds_alternative<LineId>(route_segment.carrier);
            const auto has_times = departure.has_value();

            MATHFP_TRY(ensure_trip_metadata_consistent(
                is_line, trip, from_index, to_index
            ));
            MATHFP_TRY(ensure_carrier_timing_consistent(
                is_line, has_times
            ));

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
        MATHFP_TRY(validation::ensure_nonneg(length, "length"));
        MATHFP_TRY(validation::ensure_nonneg(run_time, "run_time"));
        MATHFP_TRY(detail::ensure_distinct_endpoints(from, to));
        MATHFP_TRY(detail::ensure_route_carrier_consistent(from, to, carrier));

        return RouteSegment{
            .id         = id
            , .from     = std::move(from)
            , .to       = std::move(to)
            , .length   = length
            , .run_time = run_time
            , .carrier  = std::move(carrier)
        };
    }

    inline mathfp::Expected<ConnectionSegment> make_connection_segment(
        ConnectionSegmentId id
        , const RouteSegment& route_segment
        , std::optional<TripId> trip
        , std::optional<std::int64_t> from_index
        , std::optional<std::int64_t> to_index
        , std::optional<Time> departure
        , std::optional<Time> arrival
        , std::optional<double> fare
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
