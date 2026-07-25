#include "timetable/domain/assignment/capacity_aware/diagnostics.hpp"

#include <cmath>

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
                mathfp::invalid_arg("capacity-aware assignment scalar must be finite and non-negative")
                    .ctx("field", field)
                    .ctx("value", value)
            );
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_split_diagnostics(
        const CapacityAwareSplitDiagnostics& diagnostics
    ) {
        if (diagnostics.iterations < 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split diagnostics iterations must be non-negative")
                    .ctx("iterations", diagnostics.iterations)
            );
        }

        MATHFP_TRY(ensure_finite_nonnegative(
              diagnostics.max_load_delta
            , "max_load_delta"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              diagnostics.max_relative_load_delta
            , "max_relative_load_delta"
        ));

        if (!diagnostics.enabled) {
            if (
                   diagnostics.iterations == 0
                && !diagnostics.converged
                && diagnostics.max_load_delta == 0.0
                && diagnostics.max_relative_load_delta == 0.0
            ) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("disabled capacity-aware split diagnostics must be empty")
                    .ctx("iterations", diagnostics.iterations)
                    .ctx("converged", diagnostics.converged ? "true" : "false")
                    .ctx("max_load_delta", diagnostics.max_load_delta)
                    .ctx("max_relative_load_delta", diagnostics.max_relative_load_delta)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_diagnostics(
        const CapacityAwareAssignmentDiagnostics& diagnostics
    ) {
        MATHFP_TRY(validate_capacity_penalty_policy(diagnostics.penalty_policy));

        if (diagnostics.iterations < 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment diagnostics iterations must be non-negative")
                    .ctx("iterations", diagnostics.iterations)
            );
        }

        MATHFP_TRY(ensure_finite_nonnegative(
              diagnostics.max_load_delta
            , "max_load_delta"
        ));

        const auto used_factor = mathfp::units::as_dimless(diagnostics.used_factor);
        MATHFP_TRY(ensure_finite_nonnegative(used_factor, "used_factor"));

        if (diagnostics.capacity_aware_search_enabled
            && !diagnostics.capacity_aware_enabled) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search diagnostics require enabled capacity-aware assignment diagnostics")
            );
        }

        if (!diagnostics.capacity_aware_enabled) {
            if (
                   !diagnostics.capacity_aware_search_enabled
                && diagnostics.iterations == 0
                && !diagnostics.converged
                && diagnostics.max_load_delta == 0.0
                && used_factor == 0.0
            ) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("disabled capacity-aware assignment diagnostics must be empty")
                    .ctx("iterations", diagnostics.iterations)
                    .ctx(
                          "capacity_aware_search_enabled"
                        , diagnostics.capacity_aware_search_enabled ? "true" : "false"
                    )
                    .ctx("converged", diagnostics.converged ? "true" : "false")
                    .ctx("max_load_delta", diagnostics.max_load_delta)
                    .ctx("used_factor", used_factor)
            );
        }

        if (diagnostics.iterations <= 0 || !(used_factor > 0.0)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("enabled capacity-aware assignment diagnostics require iterations and positive used factor")
                    .ctx("iterations", diagnostics.iterations)
                    .ctx("used_factor", used_factor)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<CapacityAwareAssignmentDiagnostics> make_capacity_aware_assignment_diagnostics(
          bool                                  capacity_aware_enabled
        , bool                                  capacity_aware_search_enabled
        , const CapacityAwareSplitDiagnostics& split_diagnostics
        , Dimless                               used_factor
        , CapacityPenaltyPolicy                 penalty_policy
    ) {
        MATHFP_TRY(validate_capacity_aware_split_diagnostics(split_diagnostics));

        if (capacity_aware_enabled != split_diagnostics.enabled) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment diagnostics enabled flag disagrees with split diagnostics")
                    .ctx("capacity_aware_enabled", capacity_aware_enabled ? "true" : "false")
                    .ctx("split_enabled", split_diagnostics.enabled ? "true" : "false")
            );
        }

        CapacityAwareAssignmentDiagnostics diagnostics{
              .capacity_aware_enabled        = capacity_aware_enabled
            , .capacity_aware_search_enabled = capacity_aware_search_enabled
            , .iterations                    = split_diagnostics.iterations
            , .converged                     = split_diagnostics.converged
            , .max_load_delta                = split_diagnostics.max_load_delta
            , .used_factor                   = capacity_aware_enabled ? used_factor : Dimless{ 0.0 }
            , .penalty_policy                = penalty_policy
        };

        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(diagnostics));
        return diagnostics;
    }

    CapacityAwareAssignmentDiagnostics make_capacity_aware_assignment_disabled_diagnostics(
        CapacityPenaltyPolicy penalty_policy
    ) {
        return CapacityAwareAssignmentDiagnostics{
              .capacity_aware_enabled        = false
            , .capacity_aware_search_enabled = false
            , .iterations                    = 0
            , .converged                     = false
            , .max_load_delta                = 0.0
            , .used_factor                   = Dimless{ 0.0 }
            , .penalty_policy                = penalty_policy
        };
    }

    CapacityAwareSplitDiagnostics make_capacity_aware_split_disabled_diagnostics() {
        return CapacityAwareSplitDiagnostics{
              .enabled                 = false
            , .iterations              = 0
            , .converged               = false
            , .max_load_delta          = 0.0
            , .max_relative_load_delta = 0.0
        };
    }

}  // namespace timetable::domain::assignment
