#include "timetable/domain/assignment/capacity.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>

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

    mathfp::Expected<mathfp::Unit> validate_elementary_segment_overload_assessment(
        const ElementarySegmentOverloadAssessment& assessment
    ) {
        return validate_vehicle_journey_item_overload_assessment(assessment);
    }

}  // namespace timetable::domain::assignment
