#include "pipeline_steps.hpp"

#include <cstdint>
#include <utility>

#include <fmt/format.h>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail {

    mathfp::Expected<AssignmentPipelineCalculatedResult>
    run_capacity_aware_assignment_iteration_layer(
          AssignmentInput      input
        , PreprocessedNetwork  network
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("assignment: capacity-aware fixed-point iteration");
        log(
            fmt::format(
                  "capacity-aware assignment input: max_iterations = {}  abs_tol = {}  rel_tol = {}  search_mode = {}  split_enabled = {}  penalty_policy = {}"
                , input.capacity_aware_assignment.iteration.max_iterations
                , input.capacity_aware_assignment.iteration.absolute_load_tolerance
                , input.capacity_aware_assignment.iteration.relative_load_tolerance
                , to_string(input.capacity_aware_assignment.search_mode)
                , capacity_aware_split_enabled(input) ? "true" : "false"
                , to_string(input.capacity_aware_assignment.penalty_policy)
            )
            , LogLevel::Info
        );

        VehicleJourneyItemLoadState current_state{};
        ConnectionSearchResult      last_search{};
        ConnectionChoiceResult      last_choice{};
        DemandSplitResult           last_split{};
        VehicleJourneyItemLoads     last_loads{};
        CapacityAwareSplitDiagnostics diagnostics{
              .enabled                 = true
            , .iterations              = 0
            , .converged               = false
            , .max_load_delta          = 0.0
            , .max_relative_load_delta = 0.0
        };

        for (std::int32_t iteration = 1;
             iteration <= input.capacity_aware_assignment.iteration.max_iterations;
             ++iteration) {
            /*
             * The load state is fixed for the whole search/choice/split
             * evaluation below. This is the correctness condition that keeps
             * branch-and-bound dominance and suffix lower bounds valid under
             * capacity-aware search: one iteration optimizes one immutable
             * generalized-cost function.
             */
            MATHFP_TRY_LET(
                  SearchStepResult
                , search_step
                , run_validated_search_step(
                      network
                    , input
                    , current_state
                    , SearchDiagnosticsContext{
                          .capacity_iteration = iteration
                      }
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
                  DemandSplitResult
                , split_result
                , run_validated_single_split_step(
                      choice_result
                    , input
                    , current_state
                )
            );
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoads
                , split_loads
                , build_vehicle_journey_item_loads(split_result)
            );

            const auto alpha = 1.0 / static_cast<double>(iteration);
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoadState
                , next_state
                , msa_update_load_state(
                      current_state
                    , split_loads
                    , alpha
                )
            );

            const auto delta = load_state_delta(current_state, next_state);
            diagnostics.iterations              = iteration;
            diagnostics.max_load_delta          = delta.max_absolute;
            diagnostics.max_relative_load_delta = delta.max_relative;

            last_search = std::move(search_step.result);
            last_choice = std::move(choice_result);
            last_split  = std::move(split_result);
            last_loads  = std::move(split_loads);
            current_state = std::move(next_state);

            if (capacity_iteration_converged(
                  input.capacity_aware_assignment.iteration
                , delta
            )) {
                diagnostics.converged = true;
                break;
            }
        }

        MATHFP_TRY(validate_capacity_aware_split_step_output(
              CapacityAwareDemandSplitResult{
                    .split_result = last_split
                  , .split_loads  = last_loads
                  , .load_state   = current_state
                  , .diagnostics  = diagnostics
              }
            , last_choice
            , input.input
            , input.capacity_aware_assignment
        ));
        MATHFP_TRY_LET(
              CapacityAwareAssignmentDiagnostics
            , capacity_aware
            , make_capacity_aware_assignment_diagnostics(
                  true
                , true
                , diagnostics
                , capacity_aware_assignment_used_factor(input)
                , input.capacity_aware_assignment.penalty_policy
            )
        );

        if (!diagnostics.converged) {
            log(
                  "capacity-aware assignment reached max_iterations before convergence; using last evaluated search/choice/split"
                , LogLevel::Warning
            );
        }

        log(
            fmt::format(
                  "capacity-aware assignment result: iterations = {}  converged = {}  max_load_delta = {}  max_relative_load_delta = {}"
                , diagnostics.iterations
                , diagnostics.converged ? "true" : "false"
                , diagnostics.max_load_delta
                , diagnostics.max_relative_load_delta
            )
            , LogLevel::Info
        );
        both("assignment: capacity-aware fixed-point iteration done");

        return AssignmentPipelineCalculatedResult{
              .input                         = std::move(input.input)
            , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
            , .network                       = std::move(network)
            , .search                        = std::move(last_search)
            , .choice                        = std::move(last_choice)
            , .split                         = std::move(last_split)
            , .execution                     = input.execution
            , .skim_config                   = input.skim_matrix
            , .capacity_aware                = capacity_aware
        };
    }


}  // namespace timetable::domain::assignment::detail
