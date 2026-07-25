#include "timetable/domain/assignment/capacity/overload.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool finite_nonnegative(
            double value
        ) noexcept {
            return std::isfinite(value) && value >= 0.0;
        }

    }  // namespace

    mathfp::Expected<VehicleJourneyItemOverloadStatus>
    vehicle_journey_item_overload_status(
          double passengers
        , double total_capacity
    ) {
        if (!finite_nonnegative(passengers)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("overload status requires finite non-negative passengers")
                    .ctx("passengers", passengers)
            );
        }
        if (!finite_nonnegative(total_capacity)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("overload status requires finite non-negative capacity")
                    .ctx("total_capacity", total_capacity)
            );
        }

        if (total_capacity == 0.0) {
            return VehicleJourneyItemOverloadStatus::ZeroCapacity;
        }
        if (passengers > total_capacity) {
            return VehicleJourneyItemOverloadStatus::Overloaded;
        }
        return VehicleJourneyItemOverloadStatus::Ok;
    }

    mathfp::Expected<VehicleJourneyItemOverload>
    make_missing_capacity_vehicle_journey_item_overload(
        const VehicleJourneyItemLoad& load
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_load(load));

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

    mathfp::Expected<VehicleJourneyItemOverload>
    compute_vehicle_journey_item_overload(
          const VehicleJourneyItemLoad&     load
        , const VehicleJourneyItemCapacity& capacity
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_load(load));
        MATHFP_TRY(validate_vehicle_journey_item_capacity(capacity));

        if (load.key.item != capacity.key) {
            return mathfp::unexpected(
                mathfp::invalid_arg("vehicle journey item overload load and capacity keys do not match")
                    .ctx("load_trip_id", load.key.item.trip.get())
                    .ctx("load_from_index", load.key.item.from_index.get())
                    .ctx("capacity_trip_id", capacity.key.trip.get())
                    .ctx("capacity_from_index", capacity.key.from_index.get())
            );
        }

        MATHFP_TRY_LET(
              VehicleJourneyItemOverloadStatus
            , status
            , vehicle_journey_item_overload_status(
                  load.passengers
                , capacity.total_capacity
              )
        );
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

}  // namespace timetable::domain::assignment
