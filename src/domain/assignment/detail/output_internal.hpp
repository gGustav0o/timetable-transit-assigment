#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "grouping.hpp"
#include "timetable/domain/assignment/output.hpp"

namespace timetable::domain::assignment::detail {

    mathfp::Expected<const TimeInterval*> find_interval(
          const InputModel& input
        , IntervalId        interval_id
    );

    mathfp::Expected<AssignmentConnection> build_assignment_connection(
          const SearchConnection& connection
        , const PreprocessedNetwork&  network
    );

    mathfp::Expected<mathfp::Unit> validate_od_result_semantics(
        const AssignmentOdResult& od_result
    );

    mathfp::Expected<mathfp::Unit> validate_output_summary_semantics(
        const AssignmentOutput& output
    );

    mathfp::Expected<AssignmentLoads> build_assignment_loads(
        const DemandSplitResult& split_result
    );

    mathfp::Expected<AssignmentOutput> build_assignment_output_impl(
          const InputModel&                      input
        , const PreprocessedNetwork&             network
        , const ConnectionSearchResult&          search_result
        , const ConnectionChoiceResult&          choice_result
        , const DemandSplitResult&               split_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

    mathfp::Expected<AssignmentOutput> build_od_day_assignment_output_impl(
          const InputModel&                      input
        , const PreprocessedNetwork&             network
        , const OdDayConnectionSearchSummary&    search_summary
        , const OdDayConnectionChoiceResult&     choice_result
        , const DemandSplitResult&               split_result
        , const ElementarySegmentLoads&          elementary_segment_loads
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const AssignmentPeriodConfig&           assignment_period
        , const ConnectionAdmissibilityConfig&    admissibility_config
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

    mathfp::Expected<AssignmentOutput> build_all_zone_search_output_impl(
          const InputModel&                      input
        , const AllZoneConnectionSearchResult&   search_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

    mathfp::Expected<AssignmentOutput> build_assignment_disabled_output_impl(
          const InputModel&                      input
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

}  // namespace timetable::domain::assignment::detail
