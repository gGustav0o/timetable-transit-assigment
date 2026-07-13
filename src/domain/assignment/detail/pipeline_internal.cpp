#include "pipeline_internal.hpp"

#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "pipeline_steps.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail {

    mathfp::Expected<AssignmentPipelineResult> run_timetable_assignment_pipeline_with_context(
        AssignmentInput input
    ) {
        timetable::infra::progress::both(
              "assignment pipeline started"
            , timetable::infra::LogLevel::Info
        );
        log_search_execution_summary(input.search_execution);
        log_assignment_output_export_profile(input.execution);
        MATHFP_TRY(validate_assignment_execution_config(input.execution));
        MATHFP_TRY(validate_assignment_output_export_profile(
              input.execution
            , input.search_execution
        ));

        if (!input.execution.calculate_assignment) {
            timetable::infra::progress::both(
                  "assignment pipeline disabled by basePara.calcAssign=false"
                , timetable::infra::LogLevel::Info
            );
            return AssignmentPipelineDisabledResult{
                  .input                         = std::move(input.input)
                , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
                , .execution                     = input.execution
                , .skim_config                   = input.skim_matrix
                , .capacity_aware                =
                      make_capacity_aware_assignment_disabled_diagnostics(
                          input.capacity_aware_assignment.penalty_policy
                      )
            };
        }
        MATHFP_TRY(validate_assignment_search_execution_profile(input.search_execution));

        MATHFP_TRY_LET(
              PreprocessedNetwork
            , network
            , run_validated_preprocessing_step(input)
        );

        if (input.search_execution.formulation
            == AssignmentCalculationFormulation::AllZoneSearch) {
            MATHFP_TRY_LET(
                  SearchStepResult
                , search_step
                , run_validated_search_step(
                      network
                    , input
                    , VehicleJourneyItemLoadState{}
                    , SearchDiagnosticsContext{}
                  )
            );
            if (!search_step.all_zone_result.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("all-zone search projection did not materialize all-zone result")
                );
            }
            return AssignmentPipelineAllZoneSearchResult{
                  .input                         = std::move(input.input)
                , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
                , .network                       = std::move(network)
                , .search                        = std::move(*search_step.all_zone_result)
                , .execution                     = input.execution
                , .skim_config                   = input.skim_matrix
                , .capacity_aware                =
                      make_capacity_aware_assignment_disabled_diagnostics(
                          input.capacity_aware_assignment.penalty_policy
                      )
            };
        }

        if (input.search_execution.formulation
            == AssignmentCalculationFormulation::OdDayAssignment) {
            MATHFP_TRY_LET(
                  AssignmentPipelineOdDayCalculatedResult
                , result
                , run_od_day_assignment_layer(
                      std::move(input)
                    , std::move(network)
                )
            );
            return result;
        }

        if (input.search_execution.formulation
            == AssignmentCalculationFormulation::TimedConnectionDiagnostics) {
            timetable::infra::progress::both(
                  "assignment: timed connection diagnostics contour"
                , timetable::infra::LogLevel::Warning
            );
            timetable::infra::progress::log(
                  "timed connection diagnostics is search-only: choice=false split=false load=false"
                , timetable::infra::LogLevel::Warning
            );
            if (capacity_aware_search_enabled(input) || capacity_aware_split_enabled(input)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("timed connection diagnostics contour does not support capacity-aware assignment")
                );
            }
            MATHFP_TRY_LET(
                  SearchStepResult
                , search_step
                , run_validated_search_step(
                      network
                    , input
                    , VehicleJourneyItemLoadState{}
                    , SearchDiagnosticsContext{}
                  )
            );
            return AssignmentPipelineTimedDiagnosticsResult{
                  .input                         = std::move(input.input)
                , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
                , .network                       = std::move(network)
                , .search                        = std::move(search_step.result)
                , .execution                     = input.execution
                , .skim_config                   = input.skim_matrix
                , .capacity_aware                =
                      make_capacity_aware_assignment_disabled_diagnostics(
                          input.capacity_aware_assignment.penalty_policy
                      )
            };
        }

        if (capacity_aware_search_enabled(input)) {
            MATHFP_TRY_LET(
                  AssignmentPipelineCalculatedResult
                , result
                , run_capacity_aware_assignment_iteration_layer(
                      std::move(input)
                    , std::move(network)
                )
            );
            return result;
        }

        MATHFP_TRY_LET(
              SearchStepResult
            , search_step
            , run_validated_search_step(
                  network
                , input
                , VehicleJourneyItemLoadState{}
                , SearchDiagnosticsContext{}
              )
        );
        MATHFP_TRY_LET(
              ConnectionChoiceResult
            , choice_result
            , run_validated_choice_step(
                  search_step.result
                , input.params
                , search_step.search_cost
                , input.choice
                , input.assignment_period
                , ConnectionAdmissibilityConfig{
                      .deletion    = input.connection_deletion
                    , .demand_time = input.demand_segment_time
                  }
            )
        );
        MATHFP_TRY_LET(
              SplitStepResult
            , split_step
            , run_validated_split_step(
                  choice_result
                , input
            )
        );

        return AssignmentPipelineCalculatedResult{
              .input                         = std::move(input.input)
            , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
            , .network                       = std::move(network)
            , .search                        = std::move(search_step.result)
            , .choice                        = std::move(choice_result)
            , .split                         = std::move(split_step.result)
            , .execution                     = input.execution
            , .skim_config                   = input.skim_matrix
            , .capacity_aware                = split_step.capacity_aware
        };
    }


}  // namespace timetable::domain::assignment::detail
