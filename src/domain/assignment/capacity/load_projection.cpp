#include "timetable/domain/assignment/capacity/load_projection.hpp"

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool finite_nonnegative(
            double value
        ) noexcept {
            return std::isfinite(value) && value >= 0.0;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_nonnegative_id(
              std::int64_t raw
            , const char*  name
        ) {
            if (raw >= 0) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item key must not contain negative ids")
                    .ctx("field", name)
                    .ctx("value", raw)
            );
        }

        [[nodiscard]] VehicleJourneyItemLoad make_vehicle_journey_item_load(
              const VehicleJourneyItemLoadKey& key
            , double                           passengers
        ) noexcept {
            return VehicleJourneyItemLoad{
                  .key        = key
                , .passengers = passengers
            };
        }

        using VehicleJourneyItemLoadMap =
            std::map<VehicleJourneyItemLoadKey, mathfp::CompensatedSum<double>>;

        mathfp::Expected<mathfp::Unit> accumulate_share_vehicle_journey_item_loads(
              VehicleJourneyItemLoadMap&   loads
            , const ConnectionDemandShare& share
        ) {
            if (!(share.passengers > 0.0)) {
                return mathfp::kUnit;
            }

            if (share.source == DemandShareAlternativeSource::DayPath) {
                if (!share.day_path_support.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("day-path elementary load share is missing compact support")
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }

                for (const auto& leg : share.day_path_support->ride_legs) {
                    for (auto index = leg.from_index.get(); index < leg.to_index.get(); ++index) {
                        loads[VehicleJourneyItemLoadKey{
                              .interval = share.interval
                            , .item     = VehicleJourneyItemKey{
                                  .trip       = leg.trip
                                , .from_index = RoutePosition{ index }
                              }
                        }].add(share.passengers);
                    }
                }
                return mathfp::kUnit;
            }

            const auto& trace = canonical_connection(share.connection).trace;
            for (const auto& leg : trace.legs) {
                if (!is_ride_leg(leg.kind)) {
                    continue;
                }

                MATHFP_TRY_LET(
                      std::vector<VehicleJourneyItemKey>
                    , occupied_items
                    , vehicle_journey_items_occupied(leg)
                );

                for (const auto& item : occupied_items) {
                    loads[VehicleJourneyItemLoadKey{
                          .interval = share.interval
                        , .item     = item
                    }].add(share.passengers);
                }
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_day_path_load_share(
              const ConnectionDemandShare& share
            , std::size_t                  share_index
        ) {
            if (share.source != DemandShareAlternativeSource::DayPath) {
                return mathfp::unexpected(
                    mathfp::internal_error("day-path elementary loads require day-path split shares")
                        .ctx("share_index", static_cast<std::int64_t>(share_index))
                        .ctx("origin"     , share.origin.get())
                        .ctx("destination", share.destination.get())
                        .ctx("interval_id", share.interval.get())
                );
            }

            if (!share.day_path_support.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("day-path split share is missing compact support for elementary loads")
                        .ctx("share_index", static_cast<std::int64_t>(share_index))
                        .ctx("origin"     , share.origin.get())
                        .ctx("destination", share.destination.get())
                        .ctx("interval_id", share.interval.get())
                );
            }

            if (!(share.day_path_support->signature == share.day_path)) {
                return mathfp::unexpected(
                    mathfp::internal_error("day-path split share signature disagrees with compact support")
                        .ctx("share_index", static_cast<std::int64_t>(share_index))
                        .ctx("origin"     , share.origin.get())
                        .ctx("destination", share.destination.get())
                        .ctx("interval_id", share.interval.get())
                );
            }

            return mathfp::kUnit;
        }

        [[nodiscard]] VehicleJourneyItemLoads materialize_vehicle_journey_item_loads(
            const VehicleJourneyItemLoadMap& load_map
        ) {
            VehicleJourneyItemLoads loads;
            loads.items.reserve(load_map.size());

            for (const auto& [key, passengers] : load_map) {
                loads.items.push_back(
                    make_vehicle_journey_item_load(key, passengers.value())
                );
            }

            return loads;
        }

        [[nodiscard]] bool almost_equal_scalar(
              double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

        [[nodiscard]] double actual_vehicle_journey_item_load_sum(
            const VehicleJourneyItemLoads& loads
        ) noexcept {
            return mathfp::compensated_sum_by(
                  loads.items
                , [](const VehicleJourneyItemLoad& load) {
                      return load.passengers;
                  }
            );
        }

        mathfp::Expected<double> expected_vehicle_journey_item_load_sum(
            const DemandSplitResult& split_result
        ) {
            mathfp::CompensatedSum<double> expected;

            for (const auto& share : split_result.shares) {
                if (!(share.passengers > 0.0)) {
                    continue;
                }

                if (share.source == DemandShareAlternativeSource::DayPath) {
                    if (!share.day_path_support.has_value()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("day-path load projection share is missing compact support")
                                .ctx("origin"     , share.origin.get())
                                .ctx("destination", share.destination.get())
                                .ctx("interval_id", share.interval.get())
                        );
                    }
                    for (const auto& leg : share.day_path_support->ride_legs) {
                        expected.add(
                            share.passengers
                            * static_cast<double>(leg.to_index.get() - leg.from_index.get())
                        );
                    }
                } else {
                    const auto& trace = canonical_connection(share.connection).trace;
                    for (const auto& leg : trace.legs) {
                        if (!is_ride_leg(leg.kind)) {
                            continue;
                        }

                        MATHFP_TRY_LET(
                              std::vector<VehicleJourneyItemKey>
                            , occupied_items
                            , vehicle_journey_items_occupied(leg)
                        );
                        expected.add(
                            share.passengers * static_cast<double>(occupied_items.size())
                        );
                    }
                }
            }

            return expected.value();
        }

    }  // namespace

    mathfp::Expected<std::vector<VehicleJourneyItemKey>> vehicle_journey_items_occupied(
          TripId        trip
        , RoutePosition from_index
        , RoutePosition to_index
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_key(
            VehicleJourneyItemKey{
                  .trip       = trip
                , .from_index = from_index
            }
        ));
        MATHFP_TRY(ensure_nonnegative_id(to_index.get(), "to_index"));

        if (!(from_index < to_index)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item occupancy interval must satisfy from_index < to_index")
                    .ctx("trip_id"   , trip.get())
                    .ctx("from_index", from_index.get())
                    .ctx("to_index"  , to_index.get())
            );
        }

        const auto first = from_index.get();
        const auto last  = to_index.get();

        std::vector<VehicleJourneyItemKey> items;
        items.reserve(static_cast<std::size_t>(last - first));

        for (auto index = first; index < last; ++index) {
            items.push_back(
                VehicleJourneyItemKey{
                      .trip       = trip
                    , .from_index = RoutePosition{ index }
                }
            );
        }

        return items;
    }

    mathfp::Expected<std::vector<VehicleJourneyItemKey>> vehicle_journey_items_occupied(
        const ConnectionLeg& leg
    ) {
        if (!is_ride_leg(leg.kind)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item occupancy can only be projected from ride legs")
                    .ctx("leg_kind", std::string(to_string(leg.kind)))
            );
        }

        if (
               !leg.trip.has_value()
            || !leg.occurrence_from.has_value()
            || !leg.occurrence_to.has_value()
        ) {
            return mathfp::unexpected(
                mathfp::invalid_arg("ride leg is missing vehicle journey item occupancy identity")
                    .ctx("has_trip"           , leg.trip.has_value() ? "true" : "false")
                    .ctx("has_occurrence_from", leg.occurrence_from.has_value() ? "true" : "false")
                    .ctx("has_occurrence_to"  , leg.occurrence_to.has_value() ? "true" : "false")
            );
        }

        return vehicle_journey_items_occupied(
              *leg.trip
            , leg.occurrence_from->position
            , leg.occurrence_to->position
        );
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load(
        const VehicleJourneyItemLoad& load
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_key(load.key.item));
        MATHFP_TRY(ensure_nonnegative_id(load.key.interval.get(), "interval"));

        if (!finite_nonnegative(load.passengers)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item load passengers must be finite and non-negative")
                    .ctx("interval_id", load.key.interval.get())
                    .ctx("trip_id"    , load.key.item.trip.get())
                    .ctx("from_index" , load.key.item.from_index.get())
                    .ctx("passengers" , load.passengers)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_loads(
        const VehicleJourneyItemLoads& loads
    ) {
        std::map<VehicleJourneyItemLoadKey, std::size_t> seen;

        for (std::size_t i = 0; i < loads.items.size(); ++i) {
            const auto& load = loads.items[i];
            MATHFP_TRY(validate_vehicle_journey_item_load(load));

            if (const auto [existing, inserted] = seen.emplace(load.key, i); !inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate vehicle journey item load key")
                        .ctx("interval_id"        , load.key.interval.get())
                        .ctx("trip_id"            , load.key.item.trip.get())
                        .ctx("from_index"         , load.key.item.from_index.get())
                        .ctx("first_load_index"   , static_cast<std::int64_t>(existing->second))
                        .ctx("current_load_index" , static_cast<std::int64_t>(i))
                );
            }
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load_projection(
          const DemandSplitResult&       split_result
        , const VehicleJourneyItemLoads& loads
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_loads(loads));
        MATHFP_TRY_LET(
              double
            , expected
            , expected_vehicle_journey_item_load_sum(split_result)
        );
        const auto actual = actual_vehicle_journey_item_load_sum(loads);

        if (!almost_equal_scalar(expected, actual)) {
            return mathfp::unexpected(
                mathfp::internal_error("vehicle journey item loads are not a half-open occupancy projection of split shares")
                    .ctx("expected_item_load_sum", expected)
                    .ctx("actual_item_load_sum"  , actual)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_elementary_segment_loads(
        const ElementarySegmentLoads& loads
    ) {
        return validate_vehicle_journey_item_loads(loads);
    }

    mathfp::Expected<mathfp::Unit> validate_elementary_segment_load_projection(
          const DemandSplitResult&      split_result
        , const ElementarySegmentLoads& loads
    ) {
        return validate_vehicle_journey_item_load_projection(split_result, loads);
    }

    mathfp::Expected<VehicleJourneyItemLoads> build_vehicle_journey_item_loads(
        const DemandSplitResult& split_result
    ) {
        VehicleJourneyItemLoadMap load_map;
        for (const auto& share : split_result.shares) {
            MATHFP_TRY(accumulate_share_vehicle_journey_item_loads(load_map, share));
        }

        auto loads = materialize_vehicle_journey_item_loads(load_map);
        MATHFP_TRY(validate_vehicle_journey_item_loads(loads));
        MATHFP_TRY(validate_vehicle_journey_item_load_projection(split_result, loads));
        return loads;
    }

    mathfp::Expected<ElementarySegmentLoads> build_elementary_segment_loads(
        const DemandSplitResult& split_result
    ) {
        return build_vehicle_journey_item_loads(split_result);
    }

    mathfp::Expected<ElementarySegmentLoads> build_day_path_elementary_segment_loads(
        const DemandSplitResult& split_result
    ) {
        for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
            MATHFP_TRY(validate_day_path_load_share(split_result.shares[i], i));
        }
        MATHFP_TRY_LET(
              ElementarySegmentLoads
            , loads
            , build_vehicle_journey_item_loads(split_result)
        );
        MATHFP_TRY(validate_elementary_segment_load_projection(split_result, loads));
        return loads;
    }

    mathfp::Expected<mathfp::Unit> accumulate_elementary_segment_loads(
          ElementarySegmentLoadAccumulator& accumulator
        , const ElementarySegmentLoads&     loads
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_loads(loads));
        for (const auto& load : loads.items) {
            accumulator.loads[load.key].add(load.passengers);
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<ElementarySegmentLoads> materialize_elementary_segment_loads(
        const ElementarySegmentLoadAccumulator& accumulator
    ) {
        VehicleJourneyItemLoads loads;
        loads.items.reserve(accumulator.loads.size());
        for (const auto& [key, passengers] : accumulator.loads) {
            loads.items.push_back(
                make_vehicle_journey_item_load(key, passengers.value())
            );
        }
        MATHFP_TRY(validate_vehicle_journey_item_loads(loads));
        return loads;
    }

}  // namespace timetable::domain::assignment
