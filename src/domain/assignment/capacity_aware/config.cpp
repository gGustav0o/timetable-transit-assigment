#include "timetable/domain/assignment/capacity_aware/config.hpp"

#include <cmath>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

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
                mathfp::invalid_arg("capacity-aware assignment scalar must be finite and non-negative")
                    .ctx("field", field)
                    .ctx("value", value)
            );
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_search_mode(
        CapacityAwareSearchMode mode
    ) {
        switch (mode) {
            case CapacityAwareSearchMode::Disabled:
            case CapacityAwareSearchMode::StoredOnly:
            case CapacityAwareSearchMode::Enabled:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown capacity-aware search mode")
                .ctx("mode", static_cast<std::int64_t>(mode))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_iteration_config(
        const CapacityIterationConfig& config
    ) {
        if (config.max_iterations <= 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split max_iterations must be positive")
                    .ctx("max_iterations", config.max_iterations)
            );
        }

        MATHFP_TRY(ensure_finite_nonnegative(
              config.absolute_load_tolerance
            , "absolute_load_tolerance"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              config.relative_load_tolerance
            , "relative_load_tolerance"
        ));

        if (config.absolute_load_tolerance == 0.0 && config.relative_load_tolerance == 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split requires at least one positive convergence tolerance")
                    .ctx("absolute_load_tolerance", config.absolute_load_tolerance)
                    .ctx("relative_load_tolerance", config.relative_load_tolerance)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_config(
        const CapacityAwareAssignmentConfig& config
    ) {
        MATHFP_TRY(validate_capacity_aware_search_mode(config.search_mode));
        MATHFP_TRY(validate_capacity_penalty_policy(config.penalty_policy));
        MATHFP_TRY(validate_capacity_iteration_config(config.iteration));
        return mathfp::kUnit;
    }

    mathfp::Expected<CapacityIterationConfig> make_capacity_iteration_config(
          std::int32_t max_iterations
        , double       absolute_load_tolerance
        , double       relative_load_tolerance
    ) {
        CapacityIterationConfig config{
              .max_iterations          = max_iterations
            , .absolute_load_tolerance = absolute_load_tolerance
            , .relative_load_tolerance = relative_load_tolerance
        };

        MATHFP_TRY(validate_capacity_iteration_config(config));
        return config;
    }

    mathfp::Expected<CapacityAwareAssignmentConfig> make_capacity_aware_assignment_config(
          bool                    capacity_aware_split_enabled
        , CapacityAwareSearchMode search_mode
        , CapacityPenaltyPolicy   penalty_policy
        , CapacityIterationConfig iteration
    ) {
        CapacityAwareAssignmentConfig config{
              .capacity_aware_split_enabled = capacity_aware_split_enabled
            , .search_mode                  = search_mode
            , .penalty_policy               = penalty_policy
            , .iteration                    = iteration
        };

        MATHFP_TRY(validate_capacity_aware_assignment_config(config));
        return config;
    }

    CapacityAwareAssignmentConfig make_capacity_aware_assignment_disabled_config() {
        return CapacityAwareAssignmentConfig{
              .capacity_aware_split_enabled = false
            , .search_mode                  = CapacityAwareSearchMode::Disabled
            , .penalty_policy               = CapacityPenaltyPolicy::VolumeCapacityRatio
            , .iteration                    = CapacityIterationConfig{}
        };
    }

}  // namespace timetable::domain::assignment
