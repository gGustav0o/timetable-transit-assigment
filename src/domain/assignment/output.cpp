#include "timetable/domain/assignment/output.hpp"

#include "detail/output_internal.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<AssignmentOutput> build_assignment_output(
          const InputModel&                      input
        , const PreprocessedNetwork&             network
        , const ConnectionSearchResult&          search_result
        , const ConnectionChoiceResult&          choice_result
        , const DemandSplitResult&               split_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    ) {
        return detail::build_assignment_output_impl(
              input
            , network
            , search_result
            , choice_result
            , split_result
            , vehicle_journey_item_capacity
            , execution
            , skim_config
            , capacity_aware
        );
    }

    mathfp::Expected<AssignmentOutput> build_assignment_disabled_output(
          const InputModel&                      input
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    ) {
        return detail::build_assignment_disabled_output_impl(
              input
            , vehicle_journey_item_capacity
            , execution
            , skim_config
            , capacity_aware
        );
    }

}  // namespace timetable::domain::assignment
