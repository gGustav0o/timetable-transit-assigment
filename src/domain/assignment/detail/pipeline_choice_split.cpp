#include "pipeline_steps.hpp"

#include <utility>

#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail {

    mathfp::Expected<ConnectionChoiceResult> run_validated_choice_step(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , const SearchCostContext&      search_cost
        , const ChoiceConfig&           config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        MATHFP_TRY_LET(
              ConnectionChoiceResult
            , choice_result
            , choose_connections(
                  search_result
                , params
                , search_cost
                , config
                , assignment_period
                , admissibility_config
            )
        );
        MATHFP_TRY(validate_choice_step_output(
              choice_result
            , search_result
            , assignment_period
            , admissibility_config
        ));
        return choice_result;
    }

    mathfp::Expected<SplitStepResult> run_validated_split_step(
          const ConnectionChoiceResult& choice_result
        , const AssignmentInput&        input
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        const auto capacity_factor = mathfp::units::as_dimless(
            input.params.split.perceived_journey_time.volume_capacity_ratio
        );
        const auto capacity_aware_enabled =
               input.capacity_aware_assignment.capacity_aware_split_enabled
            && capacity_factor > 0.0;

        DemandSplitResult split_result{};

        if (capacity_aware_enabled) {
            MATHFP_TRY(validate_capacity_aware_split_step_input(
                  choice_result
                , input.input
                , input.params
                , input.demand_segment_time
                , input.capacity_aware_assignment
                , input.vehicle_journey_item_capacity
            ));

            MATHFP_TRY_LET(
                  CapacityAwareDemandSplitResult
                , capacity_split
                , iterate_capacity_aware_split(
                      choice_result
                    , input.input
                    , input.params
                    , input.demand_segment_time
                    , input.capacity_aware_assignment
                    , input.vehicle_journey_item_capacity.capacities
                )
            );
            MATHFP_TRY(validate_capacity_aware_split_step_output(
                  capacity_split
                , choice_result
                , input.input
                , input.capacity_aware_assignment
            ));
            MATHFP_TRY_LET(
                  CapacityAwareAssignmentDiagnostics
                , capacity_aware
                , make_capacity_aware_assignment_diagnostics(
                      true
                    , false
                    , capacity_split.diagnostics
                    , Dimless{ capacity_factor }
                    , input.capacity_aware_assignment.penalty_policy
                )
            );

            if (!capacity_split.diagnostics.converged) {
                log(
                      "capacity-aware split reached max_iterations before convergence; using last evaluated split"
                    , LogLevel::Warning
                );
            }

            split_result = std::move(capacity_split.split_result);
            return SplitStepResult{
                  .result         = std::move(split_result)
                , .capacity_aware = capacity_aware
            };
        } else {
            MATHFP_TRY(validate_split_step_input(
                  choice_result
                , input.input
                , input.params.split
                , input.demand_segment_time
            ));
            MATHFP_TRY_LET(
                  DemandSplitResult
                , ordinary_split
                , split_demand_over_connections(
                      choice_result
                    , input.input
                    , input.params
                    , input.demand_segment_time
                )
            );
            split_result = std::move(ordinary_split);
        }

        MATHFP_TRY(validate_split_step_output(
              split_result
            , choice_result
            , input.input
        ));
        return SplitStepResult{
              .result = std::move(split_result)
            , .capacity_aware =
                  make_capacity_aware_assignment_disabled_diagnostics(
                      input.capacity_aware_assignment.penalty_policy
                  )
        };
    }

    mathfp::Expected<DemandSplitResult> run_validated_single_split_step(
          const ConnectionChoiceResult&       choice_result
        , const AssignmentInput&              input
        , const VehicleJourneyItemLoadState&  fixed_load_state
    ) {
        if (capacity_aware_split_enabled(input)) {
            MATHFP_TRY(validate_capacity_aware_split_step_input(
                  choice_result
                , input.input
                , input.params
                , input.demand_segment_time
                , input.capacity_aware_assignment
                , input.vehicle_journey_item_capacity
            ));
            MATHFP_TRY_LET(
                  DemandSplitResult
                , capacity_split
                , split_demand_over_connections_capacity_aware(
                      choice_result
                    , input.input
                    , input.params
                    , input.demand_segment_time
                    , input.capacity_aware_assignment
                    , fixed_load_state
                    , input.vehicle_journey_item_capacity.capacities
                )
            );
            MATHFP_TRY(validate_split_step_output(
                  capacity_split
                , choice_result
                , input.input
            ));
            return capacity_split;
        }

        MATHFP_TRY(validate_split_step_input(
              choice_result
            , input.input
            , input.params.split
            , input.demand_segment_time
        ));
        MATHFP_TRY_LET(
              DemandSplitResult
            , ordinary_split
            , split_demand_over_connections(
                  choice_result
                , input.input
                , input.params
                , input.demand_segment_time
            )
        );
        MATHFP_TRY(validate_split_step_output(
              ordinary_split
            , choice_result
            , input.input
        ));
        return ordinary_split;
    }


}  // namespace timetable::domain::assignment::detail
