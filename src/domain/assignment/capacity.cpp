#include "timetable/domain/assignment/capacity.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

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
        using VehicleJourneyItemLoadLookup =
            std::map<VehicleJourneyItemLoadKey, const VehicleJourneyItemLoad*>;
        using VehicleJourneyItemCapacityMap =
            std::map<VehicleJourneyItemKey, const VehicleJourneyItemCapacity*>;
        using VehicleJourneyItemIntervalMap =
            std::map<IntervalId, const TimeInterval*>;
        using VehicleJourneyItemOverloadMap =
            std::map<VehicleJourneyItemLoadKey, const VehicleJourneyItemOverload*>;

        mathfp::Expected<mathfp::Unit> accumulate_share_vehicle_journey_item_loads(
              VehicleJourneyItemLoadMap&      loads
            , const ConnectionDemandShare&    share
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

        [[nodiscard]] VehicleJourneyItemCapacityMap build_capacity_lookup(
            const VehicleJourneyItemCapacitySet& capacities
        ) {
            VehicleJourneyItemCapacityMap lookup;
            for (const auto& capacity : capacities.items) {
                lookup.emplace(capacity.key, &capacity);
            }
            return lookup;
        }

        [[nodiscard]] VehicleJourneyItemLoadLookup build_load_lookup(
            const VehicleJourneyItemLoads& loads
        ) {
            VehicleJourneyItemLoadLookup lookup;
            for (const auto& load : loads.items) {
                lookup.emplace(load.key, &load);
            }
            return lookup;
        }

        mathfp::Expected<VehicleJourneyItemIntervalMap> build_interval_lookup_for_capacity_assessment(
            const std::vector<TimeInterval>& intervals
        ) {
            VehicleJourneyItemIntervalMap lookup;

            for (std::size_t i = 0; i < intervals.size(); ++i) {
                const auto& interval = intervals[i];
                MATHFP_TRY(ensure_nonnegative_id(interval.id.get(), "interval"));

                if (!(interval.start.value() < interval.end.value())) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("vehicle journey item overload assessment interval must satisfy start < end")
                            .ctx("interval_id", interval.id.get())
                            .ctx("start"      , interval.start.value())
                            .ctx("end"        , interval.end.value())
                    );
                }

                if (const auto [existing, inserted] = lookup.emplace(interval.id, &interval); !inserted) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate vehicle journey item overload assessment interval id")
                            .ctx("interval_id"           , interval.id.get())
                            .ctx("first_interval_start"  , existing->second->start.value())
                            .ctx("first_interval_end"    , existing->second->end.value())
                            .ctx("current_interval_index", static_cast<std::int64_t>(i))
                    );
                }
            }

            return lookup;
        }

        [[nodiscard]] double vehicle_journey_item_passengers_or_zero(
              const VehicleJourneyItemLoadLookup& loads
            , const VehicleJourneyItemLoadKey&    key
        ) noexcept {
            const auto load_it = loads.find(key);
            if (load_it == loads.end()) {
                return 0.0;
            }
            return load_it->second->passengers;
        }

        [[nodiscard]] VehicleJourneyItemOverload make_missing_capacity_overload(
            const VehicleJourneyItemLoad& load
        ) noexcept {
            return VehicleJourneyItemOverload{
                  .key                 = load.key
                , .passengers          = load.passengers
                , .total_capacity      = std::nullopt
                , .seat_capacity       = std::nullopt
                , .load_factor         = std::nullopt
                , .overload_passengers = std::nullopt
                , .status              = VehicleJourneyItemOverloadStatus::MissingCapacity
            };
        }

        [[nodiscard]] VehicleJourneyItemOverloadStatus capacity_status(
              double passengers
            , double total_capacity
        ) noexcept {
            if (total_capacity == 0.0) {
                return VehicleJourneyItemOverloadStatus::ZeroCapacity;
            }
            if (passengers > total_capacity) {
                return VehicleJourneyItemOverloadStatus::Overloaded;
            }
            return VehicleJourneyItemOverloadStatus::Ok;
        }

        [[nodiscard]] VehicleJourneyItemOverload make_capacity_overload(
              const VehicleJourneyItemLoad&     load
            , const VehicleJourneyItemCapacity& capacity
        ) noexcept {
            const auto status = capacity_status(load.passengers, capacity.total_capacity);
            const auto overload_passengers = std::max(
                  0.0
                , load.passengers - capacity.total_capacity
            );

            return VehicleJourneyItemOverload{
                  .key                 = load.key
                , .passengers          = load.passengers
                , .total_capacity      = capacity.total_capacity
                , .seat_capacity       = capacity.seat_capacity
                , .load_factor         = capacity.total_capacity > 0.0
                    ? std::optional<double>{ load.passengers / capacity.total_capacity }
                    : std::nullopt
                , .overload_passengers = overload_passengers
                , .status              = status
            };
        }

        [[nodiscard]] bool optional_finite_nonnegative(
            const std::optional<double>& value
        ) noexcept {
            return !value.has_value() || finite_nonnegative(*value);
        }

        [[nodiscard]] bool almost_equal_scalar(
              double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

        [[nodiscard]] bool almost_equal_optional_scalar(
              const std::optional<double>& lhs
            , const std::optional<double>& rhs
        ) noexcept {
            if (lhs.has_value() != rhs.has_value()) {
                return false;
            }
            return !lhs.has_value() || almost_equal_scalar(*lhs, *rhs);
        }

        [[nodiscard]] VehicleJourneyItemOverloadMap build_overload_lookup(
            const VehicleJourneyItemOverloadAssessment& assessment
        ) {
            VehicleJourneyItemOverloadMap lookup;
            for (const auto& overload : assessment.items) {
                lookup.emplace(overload.key, &overload);
            }
            return lookup;
        }

        mathfp::Expected<mathfp::Unit> validate_expected_overload_row(
              const VehicleJourneyItemOverload& actual
            , const VehicleJourneyItemOverload& expected
        ) {
            if (actual.key == expected.key
                && almost_equal_scalar(actual.passengers, expected.passengers)
                && almost_equal_optional_scalar(actual.total_capacity, expected.total_capacity)
                && almost_equal_optional_scalar(actual.seat_capacity, expected.seat_capacity)
                && almost_equal_optional_scalar(actual.load_factor, expected.load_factor)
                && almost_equal_optional_scalar(actual.overload_passengers, expected.overload_passengers)
                && actual.status == expected.status) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::internal_error("vehicle journey item overload row disagrees with full-support assessment semantics")
                    .ctx("interval_id"        , actual.key.interval.get())
                    .ctx("trip_id"            , actual.key.item.trip.get())
                    .ctx("from_index"         , actual.key.item.from_index.get())
                    .ctx("actual_status"      , std::string(to_string(actual.status)))
                    .ctx("expected_status"    , std::string(to_string(expected.status)))
                    .ctx("actual_passengers"  , actual.passengers)
                    .ctx("expected_passengers", expected.passengers)
            );
        }

        mathfp::Expected<mathfp::Unit> validate_loaded_item_intervals(
              const VehicleJourneyItemLoads&       loads
            , const VehicleJourneyItemIntervalMap& intervals
        ) {
            for (const auto& load : loads.items) {
                if (intervals.find(load.key.interval) == intervals.end()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("vehicle journey item load references interval outside overload assessment support")
                            .ctx("interval_id", load.key.interval.get())
                            .ctx("trip_id"    , load.key.item.trip.get())
                            .ctx("from_index" , load.key.item.from_index.get())
                    );
                }
            }

            return mathfp::kUnit;
        }

        [[nodiscard]] std::size_t missing_capacity_load_count(
              const VehicleJourneyItemLoads&       loads
            , const VehicleJourneyItemCapacityMap& capacities
        ) noexcept {
            std::size_t count = 0;
            for (const auto& load : loads.items) {
                if (capacities.find(load.key.item) == capacities.end()) {
                    ++count;
                }
            }
            return count;
        }

        mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_overload_full_support(
              const VehicleJourneyItemOverloadAssessment& assessment
            , const VehicleJourneyItemLoads&              loads
            , const VehicleJourneyItemCapacitySet&        capacities
            , const std::vector<TimeInterval>&            intervals
            , const VehicleJourneyItemLoadLookup&         load_lookup
            , const VehicleJourneyItemCapacityMap&        capacity_lookup
            , const VehicleJourneyItemIntervalMap&        interval_lookup
        ) {
            MATHFP_TRY(validate_loaded_item_intervals(loads, interval_lookup));

            const auto expected_count =
                  intervals.size() * capacities.items.size()
                + missing_capacity_load_count(loads, capacity_lookup);
            if (assessment.items.size() != expected_count) {
                return mathfp::unexpected(
                    mathfp::internal_error("vehicle journey item overload assessment row count disagrees with full reporting support")
                        .ctx("actual_row_count"  , static_cast<std::int64_t>(assessment.items.size()))
                        .ctx("expected_row_count", static_cast<std::int64_t>(expected_count))
                        .ctx("interval_count"    , static_cast<std::int64_t>(intervals.size()))
                        .ctx("capacity_count"    , static_cast<std::int64_t>(capacities.items.size()))
                );
            }

            const auto overload_lookup = build_overload_lookup(assessment);

            for (const auto& interval : intervals) {
                for (const auto& capacity : capacities.items) {
                    const auto key = VehicleJourneyItemLoadKey{
                          .interval = interval.id
                        , .item     = capacity.key
                    };
                    const auto overload_it = overload_lookup.find(key);
                    if (overload_it == overload_lookup.end()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("vehicle journey item overload assessment is missing a capacity-supported row")
                                .ctx("interval_id", key.interval.get())
                                .ctx("trip_id"    , key.item.trip.get())
                                .ctx("from_index" , key.item.from_index.get())
                        );
                    }

                    const auto expected_load = make_vehicle_journey_item_load(
                          key
                        , vehicle_journey_item_passengers_or_zero(load_lookup, key)
                    );
                    MATHFP_TRY(validate_expected_overload_row(
                          *overload_it->second
                        , make_capacity_overload(expected_load, capacity)
                    ));
                }
            }

            for (const auto& load : loads.items) {
                if (capacity_lookup.find(load.key.item) != capacity_lookup.end()) {
                    continue;
                }

                const auto overload_it = overload_lookup.find(load.key);
                if (overload_it == overload_lookup.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("vehicle journey item overload assessment is missing a missing-capacity loaded row")
                            .ctx("interval_id", load.key.interval.get())
                            .ctx("trip_id"    , load.key.item.trip.get())
                            .ctx("from_index" , load.key.item.from_index.get())
                    );
                }

                MATHFP_TRY(validate_expected_overload_row(
                      *overload_it->second
                    , make_missing_capacity_overload(load)
                ));
            }

            return mathfp::kUnit;
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

            return expected.value();
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_key(
        VehicleJourneyItemKey key
    ) {
        MATHFP_TRY(ensure_nonnegative_id(key.trip.get(), "trip"));
        MATHFP_TRY(ensure_nonnegative_id(key.from_index.get(), "from_index"));
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_capacity(
        const VehicleJourneyItemCapacity& capacity
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_key(capacity.key));

        if (!finite_nonnegative(capacity.total_capacity)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item total capacity must be finite and non-negative")
                    .ctx("trip_id"       , capacity.key.trip.get())
                    .ctx("from_index"    , capacity.key.from_index.get())
                    .ctx("total_capacity", capacity.total_capacity)
            );
        }

        if (!finite_nonnegative(capacity.seat_capacity)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item seat capacity must be finite and non-negative")
                    .ctx("trip_id"      , capacity.key.trip.get())
                    .ctx("from_index"   , capacity.key.from_index.get())
                    .ctx("seat_capacity", capacity.seat_capacity)
            );
        }

        if (capacity.seat_capacity > capacity.total_capacity) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item seat capacity must not exceed total capacity")
                    .ctx("trip_id"       , capacity.key.trip.get())
                    .ctx("from_index"    , capacity.key.from_index.get())
                    .ctx("total_capacity", capacity.total_capacity)
                    .ctx("seat_capacity" , capacity.seat_capacity)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_capacity_set(
        const VehicleJourneyItemCapacitySet& capacities
    ) {
        std::map<VehicleJourneyItemKey, std::size_t> seen;

        for (std::size_t i = 0; i < capacities.items.size(); ++i) {
            const auto& capacity = capacities.items[i];
            MATHFP_TRY(validate_vehicle_journey_item_capacity(capacity));

            if (const auto [existing, inserted] = seen.emplace(capacity.key, i); !inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate vehicle journey item capacity key")
                        .ctx("trip_id"               , capacity.key.trip.get())
                        .ctx("from_index"            , capacity.key.from_index.get())
                        .ctx("first_capacity_index"  , static_cast<std::int64_t>(existing->second))
                        .ctx("current_capacity_index", static_cast<std::int64_t>(i))
                );
            }
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<VehicleJourneyItemCapacity> make_vehicle_journey_item_capacity(
          VehicleJourneyItemKey key
        , double                total_capacity
        , double                seat_capacity
    ) {
        VehicleJourneyItemCapacity capacity{
              .key            = key
            , .total_capacity = total_capacity
            , .seat_capacity  = seat_capacity
        };

        MATHFP_TRY(validate_vehicle_journey_item_capacity(capacity));
        return capacity;
    }

    mathfp::Expected<VehicleJourneyItemCapacitySet> make_vehicle_journey_item_capacity_set(
        std::vector<VehicleJourneyItemCapacity> items
    ) {
        VehicleJourneyItemCapacitySet capacities{
            .items = std::move(items)
        };

        MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacities));
        return capacities;
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_capacity_input(
        const VehicleJourneyItemCapacityInput& input
    ) {
        switch (input.status) {
            case VehicleJourneyItemCapacityInputStatus::MissingInput:
                if (!input.capacities.items.empty()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("missing vehicle journey item capacity input must not contain capacities")
                            .ctx("capacity_count", static_cast<std::int64_t>(input.capacities.items.size()))
                    );
                }
                return mathfp::kUnit;

            case VehicleJourneyItemCapacityInputStatus::Loaded:
                return validate_vehicle_journey_item_capacity_set(input.capacities);
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown vehicle journey item capacity input status")
                .ctx("status", static_cast<std::int64_t>(input.status))
        );
    }

    VehicleJourneyItemCapacityInput make_missing_vehicle_journey_item_capacity_input() {
        return VehicleJourneyItemCapacityInput{
              .status     = VehicleJourneyItemCapacityInputStatus::MissingInput
            , .capacities = VehicleJourneyItemCapacitySet{}
        };
    }

    mathfp::Expected<VehicleJourneyItemCapacityInput> make_loaded_vehicle_journey_item_capacity_input(
        VehicleJourneyItemCapacitySet capacities
    ) {
        VehicleJourneyItemCapacityInput input{
              .status     = VehicleJourneyItemCapacityInputStatus::Loaded
            , .capacities = std::move(capacities)
        };

        MATHFP_TRY(validate_vehicle_journey_item_capacity_input(input));
        return input;
    }

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

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_overload(
        const VehicleJourneyItemOverload& overload
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_load(
            VehicleJourneyItemLoad{
                  .key        = overload.key
                , .passengers = overload.passengers
            }
        ));

        if (!optional_finite_nonnegative(overload.total_capacity)
            || !optional_finite_nonnegative(overload.seat_capacity)
            || !optional_finite_nonnegative(overload.load_factor)
            || !optional_finite_nonnegative(overload.overload_passengers)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item overload contains invalid scalar")
                    .ctx("interval_id", overload.key.interval.get())
                    .ctx("trip_id"    , overload.key.item.trip.get())
                    .ctx("from_index" , overload.key.item.from_index.get())
            );
        }

        switch (overload.status) {
            case VehicleJourneyItemOverloadStatus::MissingCapacity:
                if (!overload.total_capacity.has_value()
                    && !overload.seat_capacity.has_value()
                    && !overload.load_factor.has_value()
                    && !overload.overload_passengers.has_value()) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("missing-capacity overload row must not contain capacity-derived values")
                        .ctx("interval_id", overload.key.interval.get())
                        .ctx("trip_id"    , overload.key.item.trip.get())
                        .ctx("from_index" , overload.key.item.from_index.get())
                );

            case VehicleJourneyItemOverloadStatus::ZeroCapacity:
                if (overload.total_capacity.has_value()
                    && almost_equal_scalar(*overload.total_capacity, 0.0)
                    && overload.seat_capacity.has_value()
                    && !overload.load_factor.has_value()
                    && overload.overload_passengers.has_value()
                    && almost_equal_scalar(*overload.overload_passengers, overload.passengers)) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("zero-capacity overload row has inconsistent derived values")
                        .ctx("interval_id", overload.key.interval.get())
                        .ctx("trip_id"    , overload.key.item.trip.get())
                        .ctx("from_index" , overload.key.item.from_index.get())
                );

            case VehicleJourneyItemOverloadStatus::Ok:
                if (overload.total_capacity.has_value()
                    && *overload.total_capacity > 0.0
                    && overload.seat_capacity.has_value()
                    && overload.load_factor.has_value()
                    && overload.overload_passengers.has_value()
                    && almost_equal_scalar(
                          *overload.load_factor
                        , overload.passengers / *overload.total_capacity
                    )
                    && almost_equal_scalar(*overload.overload_passengers, 0.0)
                    && overload.passengers <= *overload.total_capacity) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("ok overload row has inconsistent derived values")
                        .ctx("interval_id", overload.key.interval.get())
                        .ctx("trip_id"    , overload.key.item.trip.get())
                        .ctx("from_index" , overload.key.item.from_index.get())
                );

            case VehicleJourneyItemOverloadStatus::Overloaded:
                if (overload.total_capacity.has_value()
                    && *overload.total_capacity > 0.0
                    && overload.seat_capacity.has_value()
                    && overload.load_factor.has_value()
                    && overload.overload_passengers.has_value()
                    && almost_equal_scalar(
                          *overload.load_factor
                        , overload.passengers / *overload.total_capacity
                    )
                    && almost_equal_scalar(
                          *overload.overload_passengers
                        , overload.passengers - *overload.total_capacity
                    )
                    && *overload.overload_passengers > 0.0
                    && overload.passengers > *overload.total_capacity) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("overloaded row has inconsistent derived values")
                        .ctx("interval_id", overload.key.interval.get())
                        .ctx("trip_id"    , overload.key.item.trip.get())
                        .ctx("from_index" , overload.key.item.from_index.get())
                );
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown vehicle journey item overload status")
                .ctx("status", static_cast<std::int64_t>(overload.status))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_overload_assessment(
        const VehicleJourneyItemOverloadAssessment& assessment
    ) {
        switch (assessment.status) {
            case VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled:
            case VehicleJourneyItemOverloadAssessmentStatus::DisabledByConfig:
            case VehicleJourneyItemOverloadAssessmentStatus::MissingCapacityInput:
                if (assessment.items.empty()) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("non-calculated vehicle journey item overload assessment must not contain rows")
                        .ctx("status", std::string(to_string(assessment.status)))
                        .ctx("row_count", static_cast<std::int64_t>(assessment.items.size()))
                );

            case VehicleJourneyItemOverloadAssessmentStatus::Calculated:
                break;
        }

        if (assessment.status != VehicleJourneyItemOverloadAssessmentStatus::Calculated) {
            return mathfp::unexpected(
                mathfp::invalid_arg("unknown vehicle journey item overload assessment status")
                    .ctx("status", static_cast<std::int64_t>(assessment.status))
            );
        }

        std::map<VehicleJourneyItemLoadKey, std::size_t> seen;

        for (std::size_t i = 0; i < assessment.items.size(); ++i) {
            const auto& overload = assessment.items[i];
            MATHFP_TRY(validate_vehicle_journey_item_overload(overload));

            if (const auto [existing, inserted] = seen.emplace(overload.key, i); !inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate vehicle journey item overload key")
                        .ctx("interval_id"            , overload.key.interval.get())
                        .ctx("trip_id"                , overload.key.item.trip.get())
                        .ctx("from_index"             , overload.key.item.from_index.get())
                        .ctx("first_overload_index"   , static_cast<std::int64_t>(existing->second))
                        .ctx("current_overload_index" , static_cast<std::int64_t>(i))
                );
            }
        }

        return mathfp::kUnit;
    }

    VehicleJourneyItemOverloadAssessment make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment() {
        return VehicleJourneyItemOverloadAssessment{
              .status = VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled
            , .items  = {}
        };
    }

    VehicleJourneyItemOverloadAssessment make_disabled_by_config_vehicle_journey_item_overload_assessment() {
        return VehicleJourneyItemOverloadAssessment{
              .status = VehicleJourneyItemOverloadAssessmentStatus::DisabledByConfig
            , .items  = {}
        };
    }

    VehicleJourneyItemOverloadAssessment make_missing_capacity_input_vehicle_journey_item_overload_assessment() {
        return VehicleJourneyItemOverloadAssessment{
              .status = VehicleJourneyItemOverloadAssessmentStatus::MissingCapacityInput
            , .items  = {}
        };
    }

    mathfp::Expected<VehicleJourneyItemOverloadAssessment> assess_vehicle_journey_item_overload(
          const VehicleJourneyItemLoads&       loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_loads(loads));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacities));
        MATHFP_TRY_LET(
              VehicleJourneyItemIntervalMap
            , interval_lookup
            , build_interval_lookup_for_capacity_assessment(intervals)
        );
        const auto capacity_lookup = build_capacity_lookup(capacities);
        const auto load_lookup     = build_load_lookup(loads);

        MATHFP_TRY(validate_loaded_item_intervals(loads, interval_lookup));

        VehicleJourneyItemOverloadAssessment assessment;
        assessment.status = VehicleJourneyItemOverloadAssessmentStatus::Calculated;
        assessment.items.reserve(
              intervals.size() * capacities.items.size()
            + loads.items.size()
        );

        for (const auto& interval : intervals) {
            for (const auto& capacity : capacities.items) {
                const auto key = VehicleJourneyItemLoadKey{
                      .interval = interval.id
                    , .item     = capacity.key
                };
                assessment.items.push_back(
                    make_capacity_overload(
                          make_vehicle_journey_item_load(
                                key
                              , vehicle_journey_item_passengers_or_zero(load_lookup, key)
                          )
                        , capacity
                    )
                );
            }
        }

        for (const auto& load : loads.items) {
            const auto capacity_it = capacity_lookup.find(load.key.item);
            if (capacity_it == capacity_lookup.end()) {
                assessment.items.push_back(make_missing_capacity_overload(load));
            }
        }

        MATHFP_TRY(validate_vehicle_journey_item_overload_assessment(assessment));
        MATHFP_TRY(validate_vehicle_journey_item_overload_full_support(
              assessment
            , loads
            , capacities
            , intervals
            , load_lookup
            , capacity_lookup
            , interval_lookup
        ));
        return assessment;
    }

    mathfp::Expected<ElementarySegmentOverloadAssessment> assess_elementary_segment_overload(
          const ElementarySegmentLoads&        loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    ) {
        return assess_vehicle_journey_item_overload(
              loads
            , capacities
            , intervals
        );
    }

}  // namespace timetable::domain::assignment
