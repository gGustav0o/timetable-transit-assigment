#include "timetable/domain/assignment/capacity_aware/penalty.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/capacity/load_projection.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool finite_nonnegative(
            double value
        ) noexcept {
            return std::isfinite(value) && value >= 0.0;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_finite_nonnegative(
              double      value
            , const char* field
        ) {
            if (finite_nonnegative(value)) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment scalar must be finite and non-negative")
                    .ctx("field", field)
                    .ctx("value", value)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_positive_capacity(
            const VehicleJourneyItemCapacity& capacity
        ) {
            if (capacity.total_capacity > 0.0) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment requires positive total capacity")
                    .ctx("trip_id", capacity.key.trip.get())
                    .ctx("from_index", capacity.key.from_index.get())
                    .ctx("total_capacity", capacity.total_capacity)
            );
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_capacity_penalty_policy(
        CapacityPenaltyPolicy policy
    ) {
        switch (policy) {
            case CapacityPenaltyPolicy::VolumeCapacityRatio:
            case CapacityPenaltyPolicy::ExcessVolumeCapacityRatio:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown capacity penalty policy")
                .ctx("policy", static_cast<std::int64_t>(policy))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_exposure(
        CapacityExposure exposure
    ) {
        return ensure_finite_nonnegative(
              exposure.equivalent_time.value()
            , "equivalent_time"
        );
    }

    mathfp::Expected<Dimless> capacity_ratio(
          double                            load
        , const VehicleJourneyItemCapacity& capacity
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(load, "load"));
        MATHFP_TRY(validate_vehicle_journey_item_capacity(capacity));
        MATHFP_TRY(ensure_positive_capacity(capacity));
        return Dimless{ load / capacity.total_capacity };
    }

    mathfp::Expected<Dimless> capacity_ratio(
          const VehicleJourneyItemLoad&     load
        , const VehicleJourneyItemCapacity& capacity
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_load(load));
        if (load.key.item != capacity.key) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity ratio load and capacity keys do not match")
                    .ctx("load_trip_id", load.key.item.trip.get())
                    .ctx("load_from_index", load.key.item.from_index.get())
                    .ctx("capacity_trip_id", capacity.key.trip.get())
                    .ctx("capacity_from_index", capacity.key.from_index.get())
            );
        }
        return capacity_ratio(load.passengers, capacity);
    }

    mathfp::Expected<Dimless> capacity_penalty(
          CapacityPenaltyPolicy policy
        , Dimless               ratio
    ) {
        MATHFP_TRY(validate_capacity_penalty_policy(policy));
        const auto raw_ratio = mathfp::units::as_dimless(ratio);
        MATHFP_TRY(ensure_finite_nonnegative(raw_ratio, "capacity_ratio"));

        switch (policy) {
            case CapacityPenaltyPolicy::VolumeCapacityRatio:
                return Dimless{ raw_ratio };

            case CapacityPenaltyPolicy::ExcessVolumeCapacityRatio:
                return Dimless{ std::max(0.0, raw_ratio - 1.0) };
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown capacity penalty policy")
                .ctx("policy", static_cast<std::int64_t>(policy))
        );
    }

    mathfp::Expected<double> capacity_adjusted_perceived_journey_time(
          double           base_perceived_journey_time
        , CapacityExposure exposure
        , Dimless          factor
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(
              base_perceived_journey_time
            , "base_perceived_journey_time"
        ));
        MATHFP_TRY(validate_capacity_exposure(exposure));
        const auto raw_factor = mathfp::units::as_dimless(factor);
        MATHFP_TRY(ensure_finite_nonnegative(raw_factor, "capacity_factor"));

        const auto adjusted = base_perceived_journey_time
            + raw_factor * exposure.equivalent_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(
              adjusted
            , "capacity_adjusted_perceived_journey_time"
        ));
        return adjusted;
    }

    mathfp::Expected<double> capacity_adjusted_split_impedance(
          double           base_split_impedance
        , CapacityExposure exposure
        , Dimless          q_time
        , Dimless          perceived_journey_time_capacity_factor
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(
              base_split_impedance
            , "base_split_impedance"
        ));
        MATHFP_TRY(validate_capacity_exposure(exposure));

        const auto raw_q_time = mathfp::units::as_dimless(q_time);
        const auto raw_factor = mathfp::units::as_dimless(
            perceived_journey_time_capacity_factor
        );
        MATHFP_TRY(ensure_finite_nonnegative(raw_q_time, "q_time"));
        MATHFP_TRY(ensure_finite_nonnegative(raw_factor, "capacity_factor"));

        const auto adjusted = base_split_impedance
            + raw_q_time * raw_factor * exposure.equivalent_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(
              adjusted
            , "capacity_adjusted_split_impedance"
        ));
        return adjusted;
    }

    mathfp::Expected<CapacityExposure> make_capacity_exposure(
        Time equivalent_time
    ) {
        CapacityExposure exposure{
            .equivalent_time = equivalent_time
        };

        MATHFP_TRY(validate_capacity_exposure(exposure));
        return exposure;
    }

}  // namespace timetable::domain::assignment
