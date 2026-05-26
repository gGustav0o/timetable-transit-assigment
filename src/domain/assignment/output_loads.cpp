#include "detail/output_internal.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

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

        struct RouteTotalLoadKey final {
            LineId  line{};
            RouteId route{};
        };

        struct StopLoadKey final {
            IntervalId interval{};
            StopId     stop{};
        };

        struct StopTotalLoadKey final {
            StopId stop{};
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

        [[nodiscard]] bool operator<(
              const RouteTotalLoadKey& lhs
            , const RouteTotalLoadKey& rhs
        ) noexcept {
            return std::tuple{ lhs.line.get(), lhs.route.get() }
                 < std::tuple{ rhs.line.get(), rhs.route.get() };
        }

        [[nodiscard]] bool operator<(
              const StopLoadKey& lhs
            , const StopLoadKey& rhs
        ) noexcept {
            return std::tuple{ lhs.interval.get(), lhs.stop.get() }
                 < std::tuple{ rhs.interval.get(), rhs.stop.get() };
        }

        [[nodiscard]] bool operator<(
              const StopTotalLoadKey& lhs
            , const StopTotalLoadKey& rhs
        ) noexcept {
            return lhs.stop.get() < rhs.stop.get();
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

        [[nodiscard]] AssignmentRouteTotalLoad make_route_total_load(
              const RouteTotalLoadKey& key
            , double                   passenger_segments
            , std::size_t              segment_load_count
        ) noexcept {
            return AssignmentRouteTotalLoad{
                  .line                = key.line
                , .route               = key.route
                , .passenger_segments  = passenger_segments
                , .segment_load_count  = segment_load_count
            };
        }

        struct LoadAggregate final {
            mathfp::CompensatedSum<double> passenger_segments{};
            std::size_t segment_load_count{};
        };

        struct StopLoadAggregate final {
            mathfp::CompensatedSum<double> boarding_passengers{};
            mathfp::CompensatedSum<double> alighting_passengers{};
            mathfp::CompensatedSum<double> transfer_boarding_passengers{};
            mathfp::CompensatedSum<double> transfer_alighting_passengers{};
            mathfp::CompensatedSum<double> incoming_passenger_segments{};
            mathfp::CompensatedSum<double> outgoing_passenger_segments{};
            mathfp::CompensatedSum<double> through_passengers{};
        };

        using SegmentLoadMap = std::map<SegmentLoadKey, mathfp::CompensatedSum<double>>;
        using LineLoadMap    = std::map<LineLoadKey, LoadAggregate>;
        using RouteLoadMap   = std::map<RouteLoadKey, LoadAggregate>;
        using RouteTotalLoadMap = std::map<RouteTotalLoadKey, LoadAggregate>;
        using TripLoadMap    = std::map<TripLoadKey, LoadAggregate>;
        using StopLoadMap    = std::map<StopLoadKey, StopLoadAggregate>;
        using StopTotalLoadMap = std::map<StopTotalLoadKey, StopLoadAggregate>;

        [[nodiscard]] bool same_trip_continuation(
              const ConnectionLeg& lhs
            , const ConnectionLeg& rhs
        ) noexcept {
            return lhs.trip.has_value()
                && rhs.trip.has_value()
                && lhs.occurrence_to.has_value()
                && rhs.occurrence_from.has_value()
                && *lhs.trip == *rhs.trip
                && lhs.occurrence_to->stop == rhs.occurrence_from->stop
                && lhs.occurrence_to->position == rhs.occurrence_from->position;
        }

        [[nodiscard]] const ConnectionLeg* previous_ride_leg(
              const std::vector<ConnectionLeg>& legs
            , std::size_t                       index
        ) noexcept {
            while (index > 0) {
                --index;
                if (is_ride_leg(legs[index].kind)) {
                    return &legs[index];
                }
            }
            return nullptr;
        }

        [[nodiscard]] const ConnectionLeg* next_ride_leg(
              const std::vector<ConnectionLeg>& legs
            , std::size_t                       index
        ) noexcept {
            for (std::size_t i = index + 1; i < legs.size(); ++i) {
                if (is_ride_leg(legs[i].kind)) {
                    return &legs[i];
                }
            }
            return nullptr;
        }

        [[nodiscard]] mathfp::Expected<StopOccurrenceKey> require_occurrence_from(
              IntervalId           interval
            , const ConnectionLeg& leg
        ) {
            if (leg.occurrence_from.has_value()) {
                return *leg.occurrence_from;
            }
            return mathfp::unexpected(
                mathfp::internal_error("ride leg is missing from-stop occurrence for stop-load projection")
                    .ctx("interval_id", interval.get())
                    .ctx("leg_kind"   , std::string(to_string(leg.kind)))
            );
        }

        [[nodiscard]] mathfp::Expected<StopOccurrenceKey> require_occurrence_to(
              IntervalId           interval
            , const ConnectionLeg& leg
        ) {
            if (leg.occurrence_to.has_value()) {
                return *leg.occurrence_to;
            }
            return mathfp::unexpected(
                mathfp::internal_error("ride leg is missing to-stop occurrence for stop-load projection")
                    .ctx("interval_id", interval.get())
                    .ctx("leg_kind"   , std::string(to_string(leg.kind)))
            );
        }

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

        mathfp::Expected<mathfp::Unit> accumulate_share_stop_load(
              StopLoadMap&                  stop_loads
            , const ConnectionDemandShare&  share
        ) {
            if (!(share.passengers > 0.0)) {
                return mathfp::kUnit;
            }

            const auto& legs = canonical_connection(share.connection).trace.legs;
            for (std::size_t i = 0; i < legs.size(); ++i) {
                const auto& leg = legs[i];
                if (!is_ride_leg(leg.kind)) {
                    continue;
                }

                MATHFP_TRY_LET(
                      StopOccurrenceKey
                    , from
                    , require_occurrence_from(share.interval, leg)
                );
                MATHFP_TRY_LET(
                      StopOccurrenceKey
                    , to
                    , require_occurrence_to(share.interval, leg)
                );

                auto& from_stop = stop_loads[StopLoadKey{
                      .interval = share.interval
                    , .stop     = from.stop
                }];
                from_stop.outgoing_passenger_segments.add(share.passengers);

                auto& to_stop = stop_loads[StopLoadKey{
                      .interval = share.interval
                    , .stop     = to.stop
                }];
                to_stop.incoming_passenger_segments.add(share.passengers);

                const auto* previous_ride = previous_ride_leg(legs, i);
                const auto* next_ride     = next_ride_leg(legs, i);

                const auto continues_from_previous =
                       previous_ride != nullptr
                    && same_trip_continuation(*previous_ride, leg);
                const auto continues_to_next =
                       next_ride != nullptr
                    && same_trip_continuation(leg, *next_ride);

                if (continues_from_previous) {
                    from_stop.through_passengers.add(share.passengers);
                } else {
                    from_stop.boarding_passengers.add(share.passengers);
                    if (previous_ride != nullptr) {
                        from_stop.transfer_boarding_passengers.add(share.passengers);
                    }
                }

                if (!continues_to_next) {
                    to_stop.alighting_passengers.add(share.passengers);
                    if (next_ride != nullptr) {
                        to_stop.transfer_alighting_passengers.add(share.passengers);
                    }
                }
            }

            return mathfp::kUnit;
        }

        [[nodiscard]] AssignmentStopLoad make_stop_load(
              const StopLoadKey&       key
            , const StopLoadAggregate& aggregate
        ) noexcept {
            return AssignmentStopLoad{
                  .interval                      = key.interval
                , .stop                          = key.stop
                , .boarding_passengers           = aggregate.boarding_passengers.value()
                , .alighting_passengers          = aggregate.alighting_passengers.value()
                , .transfer_boarding_passengers  = aggregate.transfer_boarding_passengers.value()
                , .transfer_alighting_passengers = aggregate.transfer_alighting_passengers.value()
                , .incoming_passenger_segments   = aggregate.incoming_passenger_segments.value()
                , .outgoing_passenger_segments   = aggregate.outgoing_passenger_segments.value()
                , .through_passengers            = aggregate.through_passengers.value()
            };
        }

        [[nodiscard]] AssignmentStopTotalLoad make_stop_total_load(
              const StopTotalLoadKey&  key
            , const StopLoadAggregate& aggregate
        ) noexcept {
            const auto boarding = aggregate.boarding_passengers.value();
            const auto alighting = aggregate.alighting_passengers.value();
            const auto incoming = aggregate.incoming_passenger_segments.value();
            const auto outgoing = aggregate.outgoing_passenger_segments.value();
            const auto through = aggregate.through_passengers.value();
            return AssignmentStopTotalLoad{
                  .stop                        = key.stop
                , .boarding_passengers         = boarding
                , .alighting_passengers        = alighting
                , .incoming_passenger_segments = incoming
                , .outgoing_passenger_segments = outgoing
                , .through_passengers          = through
                , .total_passenger_flow        = boarding + alighting + through
            };
        }

        [[nodiscard]] AssignmentLoads materialize_loads(
              const SegmentLoadMap& segment_load_map
            , const StopLoadMap&    stop_load_map
        ) {
            LineLoadMap line_load_map;
            RouteLoadMap route_load_map;
            RouteTotalLoadMap route_total_load_map;
            TripLoadMap trip_load_map;
            StopTotalLoadMap stop_total_load_map;

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

                auto& route_total_load = route_total_load_map[RouteTotalLoadKey{
                      .line  = key.line
                    , .route = key.route
                }];
                route_total_load.passenger_segments.add(passenger_count);
                route_total_load.segment_load_count += 1;
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

            loads.route_total_loads.reserve(route_total_load_map.size());
            for (const auto& [key, aggregate] : route_total_load_map) {
                loads.route_total_loads.push_back(
                    make_route_total_load(
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

            loads.stop_loads.reserve(stop_load_map.size());
            for (const auto& [key, aggregate] : stop_load_map) {
                loads.stop_loads.push_back(make_stop_load(key, aggregate));
                auto& stop_total = stop_total_load_map[StopTotalLoadKey{
                    .stop = key.stop
                }];
                stop_total.boarding_passengers.add(aggregate.boarding_passengers.value());
                stop_total.alighting_passengers.add(aggregate.alighting_passengers.value());
                stop_total.transfer_boarding_passengers.add(
                    aggregate.transfer_boarding_passengers.value()
                );
                stop_total.transfer_alighting_passengers.add(
                    aggregate.transfer_alighting_passengers.value()
                );
                stop_total.incoming_passenger_segments.add(
                    aggregate.incoming_passenger_segments.value()
                );
                stop_total.outgoing_passenger_segments.add(
                    aggregate.outgoing_passenger_segments.value()
                );
                stop_total.through_passengers.add(aggregate.through_passengers.value());
            }

            loads.stop_total_loads.reserve(stop_total_load_map.size());
            for (const auto& [key, aggregate] : stop_total_load_map) {
                loads.stop_total_loads.push_back(make_stop_total_load(key, aggregate));
            }

            return loads;
        }

    }  // namespace

    mathfp::Expected<AssignmentLoads> build_assignment_loads(
        const DemandSplitResult& split_result
    ) {
        SegmentLoadMap segment_loads;
        StopLoadMap    stop_loads;
        for (const auto& share : split_result.shares) {
            MATHFP_TRY(accumulate_share_load(segment_loads, share));
            MATHFP_TRY(accumulate_share_stop_load(stop_loads, share));
        }
        return materialize_loads(segment_loads, stop_loads);
    }

}  // namespace timetable::domain::assignment::detail
