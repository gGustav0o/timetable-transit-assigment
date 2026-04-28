#include "detail/output_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment::detail {
    namespace {

        struct SegmentLoadKey final {
            IntervalId          interval{};
            LineId              line{};
            RouteId             route{};
            TripId              trip{};
            RouteSegmentId      route_segment{};
            ConnectionSegmentId connection_segment{};
            StopOccurrence      from{};
            StopOccurrence      to{};
            Time                departure{};
            Time                arrival{};
        };

        struct LineLoadKey final {
            IntervalId interval{};
            LineId     line{};
        };

        struct TripLoadKey final {
            IntervalId interval{};
            LineId     line{};
            RouteId    route{};
            TripId     trip{};
        };

        struct RouteLoadKey final {
            IntervalId interval{};
            LineId     line{};
            RouteId    route{};
        };

        [[nodiscard]] bool operator<(
              const SegmentLoadKey& lhs
            , const SegmentLoadKey& rhs
        ) noexcept {
            return std::tuple{
                  lhs.interval.get()
                , lhs.line.get()
                , lhs.route.get()
                , lhs.trip.get()
                , lhs.route_segment.get()
                , lhs.connection_segment.get()
                , lhs.from.stop.get()
                , lhs.from.position.get()
                , lhs.to.stop.get()
                , lhs.to.position.get()
                , lhs.departure.value()
                , lhs.arrival.value()
            } < std::tuple{
                  rhs.interval.get()
                , rhs.line.get()
                , rhs.route.get()
                , rhs.trip.get()
                , rhs.route_segment.get()
                , rhs.connection_segment.get()
                , rhs.from.stop.get()
                , rhs.from.position.get()
                , rhs.to.stop.get()
                , rhs.to.position.get()
                , rhs.departure.value()
                , rhs.arrival.value()
            };
        }

        [[nodiscard]] bool operator<(
              const LineLoadKey& lhs
            , const LineLoadKey& rhs
        ) noexcept {
            return std::tuple{ lhs.interval.get(), lhs.line.get() }
                 < std::tuple{ rhs.interval.get(), rhs.line.get() };
        }

        [[nodiscard]] bool operator<(
              const TripLoadKey& lhs
            , const TripLoadKey& rhs
        ) noexcept {
            return std::tuple{ lhs.interval.get(), lhs.line.get(), lhs.route.get(), lhs.trip.get() }
                 < std::tuple{ rhs.interval.get(), rhs.line.get(), rhs.route.get(), rhs.trip.get() };
        }

        [[nodiscard]] bool operator<(
              const RouteLoadKey& lhs
            , const RouteLoadKey& rhs
        ) noexcept {
            return std::tuple{ lhs.interval.get(), lhs.line.get(), lhs.route.get() }
                 < std::tuple{ rhs.interval.get(), rhs.line.get(), rhs.route.get() };
        }

        [[nodiscard]] StopOccurrence stop_occurrence_from_key(
            const StopOccurrenceKey& key
        ) noexcept {
            return StopOccurrence{
                  .stop     = key.stop
                , .position = key.position
            };
        }

        [[nodiscard]] mathfp::Expected<SegmentLoadKey> segment_load_key(
              IntervalId           interval
            , const ConnectionLeg& leg
        ) {
            if (
                   !leg.line.has_value()
                || !leg.route.has_value()
                || !leg.trip.has_value()
                || !leg.route_segment.has_value()
                || !leg.connection_segment.has_value()
                || !leg.occurrence_from.has_value()
                || !leg.occurrence_to.has_value()
            ) {
                return mathfp::unexpected(
                    mathfp::internal_error("ride leg is missing load projection identity")
                        .ctx("interval_id", interval.get())
                        .ctx("leg_kind"   , std::string(to_string(leg.kind)))
                );
            }

            return SegmentLoadKey{
                  .interval           = interval
                , .line               = *leg.line
                , .route              = *leg.route
                , .trip               = *leg.trip
                , .route_segment      = *leg.route_segment
                , .connection_segment = *leg.connection_segment
                , .from               = stop_occurrence_from_key(*leg.occurrence_from)
                , .to                 = stop_occurrence_from_key(*leg.occurrence_to)
                , .departure          = leg.start_time
                , .arrival            = leg.end_time
            };
        }

        [[nodiscard]] AssignmentSegmentLoad make_segment_load(
              const SegmentLoadKey& key
            , double                passengers
        ) noexcept {
            return AssignmentSegmentLoad{
                  .interval           = key.interval
                , .line               = key.line
                , .route              = key.route
                , .trip               = key.trip
                , .route_segment      = key.route_segment
                , .connection_segment = key.connection_segment
                , .from               = key.from
                , .to                 = key.to
                , .departure          = key.departure
                , .arrival            = key.arrival
                , .passengers         = passengers
            };
        }

        [[nodiscard]] AssignmentLineLoad make_line_load(
              const LineLoadKey& key
            , double             passenger_segments
            , std::size_t        segment_load_count
        ) noexcept {
            return AssignmentLineLoad{
                  .interval            = key.interval
                , .line                = key.line
                , .passenger_segments  = passenger_segments
                , .segment_load_count  = segment_load_count
            };
        }

        [[nodiscard]] AssignmentTripLoad make_trip_load(
              const TripLoadKey& key
            , double             passenger_segments
            , std::size_t        segment_load_count
        ) noexcept {
            return AssignmentTripLoad{
                  .interval            = key.interval
                , .line                = key.line
                , .route               = key.route
                , .trip                = key.trip
                , .passenger_segments  = passenger_segments
                , .segment_load_count  = segment_load_count
            };
        }

        [[nodiscard]] AssignmentRouteLoad make_route_load(
              const RouteLoadKey& key
            , double              passenger_segments
            , std::size_t         segment_load_count
        ) noexcept {
            return AssignmentRouteLoad{
                  .interval            = key.interval
                , .line                = key.line
                , .route               = key.route
                , .passenger_segments  = passenger_segments
                , .segment_load_count  = segment_load_count
            };
        }

        struct LoadAggregate final {
            mathfp::CompensatedSum<double> passenger_segments{};
            std::size_t segment_load_count{};
        };

        using SegmentLoadMap = std::map<SegmentLoadKey, mathfp::CompensatedSum<double>>;
        using LineLoadMap    = std::map<LineLoadKey, LoadAggregate>;
        using RouteLoadMap   = std::map<RouteLoadKey, LoadAggregate>;
        using TripLoadMap    = std::map<TripLoadKey, LoadAggregate>;

        mathfp::Expected<mathfp::Unit> accumulate_share_load(
              SegmentLoadMap&               segment_loads
            , const ConnectionDemandShare&  share
        ) {
            if (!(share.passengers > 0.0)) {
                return mathfp::kUnit;
            }

            const auto& trace = canonical_connection(share.connection).trace;
            for (const auto& leg : trace.legs) {
                if (!is_ride_leg(leg.kind)) {
                    continue;
                }

                MATHFP_TRY_LET(
                      SegmentLoadKey
                    , key
                    , segment_load_key(share.interval, leg)
                );
                segment_loads[key].add(share.passengers);
            }

            return mathfp::kUnit;
        }

        [[nodiscard]] AssignmentLoads materialize_loads(
            const SegmentLoadMap& segment_load_map
        ) {
            LineLoadMap line_load_map;
            RouteLoadMap route_load_map;
            TripLoadMap trip_load_map;

            AssignmentLoads loads;
            loads.segment_loads.reserve(segment_load_map.size());

            for (const auto& [key, passengers] : segment_load_map) {
                const auto passenger_count = passengers.value();
                loads.segment_loads.push_back(make_segment_load(key, passenger_count));

                auto& line_load = line_load_map[LineLoadKey{
                      .interval = key.interval
                    , .line     = key.line
                }];
                line_load.passenger_segments.add(passenger_count);
                line_load.segment_load_count += 1;

                auto& trip_load = trip_load_map[TripLoadKey{
                      .interval = key.interval
                    , .line     = key.line
                    , .route    = key.route
                    , .trip     = key.trip
                }];
                trip_load.passenger_segments.add(passenger_count);
                trip_load.segment_load_count += 1;

                auto& route_load = route_load_map[RouteLoadKey{
                      .interval = key.interval
                    , .line     = key.line
                    , .route    = key.route
                }];
                route_load.passenger_segments.add(passenger_count);
                route_load.segment_load_count += 1;
            }

            loads.line_loads.reserve(line_load_map.size());
            for (const auto& [key, aggregate] : line_load_map) {
                loads.line_loads.push_back(
                    make_line_load(
                        key
                        , aggregate.passenger_segments.value()
                        , aggregate.segment_load_count
                    )
                );
            }

            loads.route_loads.reserve(route_load_map.size());
            for (const auto& [key, aggregate] : route_load_map) {
                loads.route_loads.push_back(
                    make_route_load(
                        key
                        , aggregate.passenger_segments.value()
                        , aggregate.segment_load_count
                    )
                );
            }

            loads.trip_loads.reserve(trip_load_map.size());
            for (const auto& [key, aggregate] : trip_load_map) {
                loads.trip_loads.push_back(
                    make_trip_load(
                        key
                        , aggregate.passenger_segments.value()
                        , aggregate.segment_load_count
                    )
                );
            }

            return loads;
        }

    }  // namespace

    mathfp::Expected<AssignmentLoads> build_assignment_loads(
        const DemandSplitResult& split_result
    ) {
        SegmentLoadMap segment_loads;
        for (const auto& share : split_result.shares) {
            MATHFP_TRY(accumulate_share_load(segment_loads, share));
        }
        return materialize_loads(segment_loads);
    }

}  // namespace timetable::domain::assignment::detail
