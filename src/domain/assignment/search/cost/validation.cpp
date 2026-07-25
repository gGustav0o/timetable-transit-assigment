#include "timetable/domain/assignment/search/cost/validation.hpp"

#include <cmath>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/search/cost/capacity_index.hpp"
#include "timetable/domain/assignment/search/cost/impedance.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_finite_nonnegative(
          double      value
        , const char* field
    ) {
        if (std::isfinite(value) && value >= 0.0) {
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

}  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_cost_mode(
        SearchCostMode mode
    ) {
        switch (mode) {
            case SearchCostMode::BaseOnly:
            case SearchCostMode::CapacityAware:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(mode))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_search_capacity_cost_config(
          const SearchCapacityCostConfig& config
        , SearchCostMode                  mode
    ) {
        MATHFP_TRY(validate_search_cost_mode(mode));
        MATHFP_TRY(validate_capacity_penalty_policy(config.penalty_policy));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              config.volume_capacity_ratio
            , "volume_capacity_ratio"
        ));

        switch (mode) {
            case SearchCostMode::BaseOnly:
                if (mathfp::units::as_dimless(config.volume_capacity_ratio) == 0.0
                    && empty_capacity_cost_support(config)) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("base-only search cost context must not carry active capacity-cost data")
                        .ctx(
                              "volume_capacity_ratio"
                            , mathfp::units::as_dimless(config.volume_capacity_ratio)
                        )
                        .ctx("load_state_items", static_cast<std::int64_t>(config.load_state.items.size()))
                        .ctx("capacity_items", static_cast<std::int64_t>(config.capacity_set.items.size()))
                );

            case SearchCostMode::CapacityAware:
                if (!(mathfp::units::as_dimless(config.volume_capacity_ratio) > 0.0)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search cost requires positive SearchImp.volCapRatioFactor")
                            .ctx(
                                  "volume_capacity_ratio"
                                , mathfp::units::as_dimless(config.volume_capacity_ratio)
                            )
                    );
                }
                if (config.capacity_set.items.empty()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search cost requires vehicle journey item capacity input")
                    );
                }
                MATHFP_TRY(validate_vehicle_journey_item_load_state(config.load_state));
                MATHFP_TRY(validate_vehicle_journey_item_capacity_set(config.capacity_set));
                MATHFP_TRY(validate_search_capacity_cost_index(config));
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(mode))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_search_cost_context(
        const SearchCostContext& context
    ) {
        MATHFP_TRY(validate_search_cost_mode(context.mode));
        MATHFP_TRY(validate_search_impedance_weights(context.impedance));
        MATHFP_TRY(ensure_finite_nonnegative(context.fare_scale, "fare_scale"));
        MATHFP_TRY(validate_search_capacity_cost_config(
              context.capacity
            , context.mode
        ));
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_cost_components(
        const SearchCostComponents& components
    ) {
        MATHFP_TRY(validate_connection_impedance_components(components.base));
        MATHFP_TRY(validate_capacity_exposure(components.capacity_exposure));
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
