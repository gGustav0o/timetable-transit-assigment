#include "timetable/domain/assignment/search/cost/impedance.hpp"

#include <cmath>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/capacity_aware/penalty.hpp"

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
            mathfp::invalid_arg("search cost scalar must be finite and non-negative")
                .ctx("field", field)
                .ctx("value", value)
        );
    }

    [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_finite_nonnegative_dimless(
          Dimless     value
        , const char* field
    ) {
        return ensure_finite_nonnegative(
              mathfp::units::as_dimless(value)
            , field
        );
    }

    [[nodiscard]] double base_search_impedance_unchecked(
          const ConnectionImpedanceComponents& components
        , const SearchImpedance&               impedance
        , double                               fare_scale
    ) noexcept {
        return connection_impedance_value(
              components
            , impedance
            , fare_scale
        );
    }

    [[nodiscard]] double capacity_adjusted_search_impedance_unchecked(
          double           base_impedance
        , CapacityExposure exposure
        , Dimless          volume_capacity_ratio
    ) noexcept {
        return base_impedance
            + mathfp::units::as_dimless(volume_capacity_ratio)
                * exposure.equivalent_time.value();
    }

}  // namespace

    mathfp::Expected<mathfp::Unit> validate_connection_impedance_components(
        const ConnectionImpedanceComponents& components
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(
              components.in_vehicle_time.value()
            , "in_vehicle_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              components.access_time.value()
            , "access_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              components.egress_time.value()
            , "egress_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              components.transfer_walk_time.value()
            , "transfer_walk_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              components.transfer_wait_time.value()
            , "transfer_wait_time"
        ));
        if (components.transfer_count.get() < 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search cost transfer count must be non-negative")
                    .ctx("transfer_count", components.transfer_count.get())
            );
        }
        MATHFP_TRY(ensure_finite_nonnegative(components.fare, "fare"));
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_impedance_weights(
        const SearchImpedance& impedance
    ) {
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.in_vehicle_time
            , "in_vehicle_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.access_time
            , "access_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.egress_time
            , "egress_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.transfer_walk_time
            , "transfer_walk_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.transfer_wait_time
            , "transfer_wait_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.transfer_count
            , "transfer_count"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.fare
            , "fare"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.volume_capacity_ratio
            , "volume_capacity_ratio"
        ));

        switch (impedance.fare_normalization.kind) {
            case FareNormalization::Kind::None:
            case FareNormalization::Kind::Mean:
            case FareNormalization::Kind::Median:
            case FareNormalization::Kind::P95:
            case FareNormalization::Kind::FixedScale:
                break;
            default:
                return mathfp::unexpected(
                    mathfp::invalid_arg("unknown fare normalization kind")
                        .ctx("kind", static_cast<std::int64_t>(impedance.fare_normalization.kind))
                );
        }

        if (!std::isfinite(impedance.fare_normalization.fixed_scale)
            || impedance.fare_normalization.fixed_scale < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("fare normalization fixed scale must be finite and non-negative")
                    .ctx("fixed_scale", impedance.fare_normalization.fixed_scale)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<double> base_search_impedance(
          const ConnectionImpedanceComponents& components
        , const SearchImpedance&               impedance
        , double                               fare_scale
    ) {
        MATHFP_TRY(validate_connection_impedance_components(components));
        MATHFP_TRY(validate_search_impedance_weights(impedance));
        MATHFP_TRY(ensure_finite_nonnegative(fare_scale, "fare_scale"));

        const auto value = base_search_impedance_unchecked(
              components
            , impedance
            , fare_scale
        );
        MATHFP_TRY(ensure_finite_nonnegative(value, "base_search_impedance"));
        return value;
    }

    mathfp::Expected<double> capacity_adjusted_search_impedance(
          double           base_impedance
        , CapacityExposure exposure
        , Dimless          volume_capacity_ratio
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(
              base_impedance
            , "base_search_impedance"
        ));
        MATHFP_TRY(validate_capacity_exposure(exposure));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              volume_capacity_ratio
            , "volume_capacity_ratio"
        ));

        const auto adjusted = capacity_adjusted_search_impedance_unchecked(
              base_impedance
            , exposure
            , volume_capacity_ratio
        );
        MATHFP_TRY(ensure_finite_nonnegative(
              adjusted
            , "capacity_adjusted_search_impedance"
        ));
        return adjusted;
    }

    mathfp::Expected<double> search_impedance(
          const SearchCostComponents& components
        , const SearchCostContext&    context
    ) {
        const auto base = base_search_impedance_unchecked(
              components.base
            , context.impedance
            , context.fare_scale
        );

        switch (context.mode) {
            case SearchCostMode::BaseOnly:
                MATHFP_TRY(ensure_finite_nonnegative(base, "base_search_impedance"));
                return base;

            case SearchCostMode::CapacityAware: {
                const auto adjusted = capacity_adjusted_search_impedance_unchecked(
                      base
                    , components.capacity_exposure
                    , context.capacity.volume_capacity_ratio
                );
                MATHFP_TRY(ensure_finite_nonnegative(
                      adjusted
                    , "capacity_adjusted_search_impedance"
                ));
                return adjusted;
            }
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(context.mode))
        );
    }

}  // namespace timetable::domain::assignment
