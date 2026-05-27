#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/skim_config.hpp"
#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Convert accumulated assignment stage results to the public output.
     */
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
    );

    /**
     * @brief Convert the OD-day assignment formulation to the public output.
     *
     * Search alternatives are represented by OD-day pair counters, while
     * chosen alternatives and split shares keep the mathematical OD/interval
     * boundary: alternatives are day-level, demand remains interval-specific.
     */
    mathfp::Expected<AssignmentOutput> build_od_day_assignment_output(
          const InputModel&                      input
        , const PreprocessedNetwork&             network
        , const OdDayPathSearchSummary&          search_summary
        , const OdDayPathChoiceResult&           choice_result
        , const DemandSplitResult&               split_result
        , const ElementarySegmentLoads&          elementary_segment_loads
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const AssignmentPeriodConfig&           assignment_period
        , const ConnectionAdmissibilityConfig&    admissibility_config
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

    /**
     * @brief Convert an all-zone/VISUM-like search enumeration to public output.
     *
     * This is a search-only projection: OD rows are completion-target counters,
     * not demand-assignment rows. Choice, split, loads, and skim are not run.
     */
    mathfp::Expected<AssignmentOutput> build_all_zone_search_output(
          const InputModel&                      input
        , const AllZoneConnectionSearchResult&   search_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

    /**
     * @brief Convert timed connection diagnostics to a search-only output.
     *
     * This contour deliberately stops before choice, split, loads and overload
     * assessment. It is not a demand-assignment result.
     */
    mathfp::Expected<AssignmentOutput> build_timed_connection_diagnostics_output(
          const InputModel&                      input
        , const ConnectionSearchResult&          search_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

    /**
     * @brief Build a canonical output for the mode where assignment is disabled.
     *
     * The output preserves the OD-time demand structure and contains no
     * calculated connections, shares, loads, or skim entries.
     */
    mathfp::Expected<AssignmentOutput> build_assignment_disabled_output(
          const InputModel&                      input
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    );

}  // namespace timetable::domain::assignment
