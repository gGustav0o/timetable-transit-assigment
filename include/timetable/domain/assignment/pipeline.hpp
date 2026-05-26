#pragma once

#include <variant>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/skim_config.hpp"
#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment {
    struct AssignmentPipelineDisabledResult final {
        InputModel                          input{};
        VehicleJourneyItemCapacityInput     vehicle_journey_item_capacity{};
        AssignmentExecutionConfig           execution{};
        SkimMatrixConfig                    skim_config{};
        CapacityAwareAssignmentDiagnostics  capacity_aware{};
    };

    struct AssignmentPipelineCalculatedResult final {
        InputModel                          input{};
        VehicleJourneyItemCapacityInput     vehicle_journey_item_capacity{};
        PreprocessedNetwork                 network{};
        ConnectionSearchResult              search{};
        ConnectionChoiceResult              choice{};
        DemandSplitResult                   split{};
        AssignmentExecutionConfig           execution{};
        SkimMatrixConfig                    skim_config{};
        CapacityAwareAssignmentDiagnostics  capacity_aware{};
    };

    struct AssignmentPipelineOdDayCalculatedResult final {
        InputModel                          input{};
        VehicleJourneyItemCapacityInput     vehicle_journey_item_capacity{};
        PreprocessedNetwork                 network{};
        OdDayConnectionSearchSummary        search{};
        OdDayConnectionChoiceResult         choice{};
        DemandSplitResult                   split{};
        ElementarySegmentLoads              elementary_segment_loads{};
        AssignmentExecutionConfig           execution{};
        AssignmentPeriodConfig              assignment_period{};
        ConnectionAdmissibilityConfig       admissibility_config{};
        SkimMatrixConfig                    skim_config{};
        CapacityAwareAssignmentDiagnostics  capacity_aware{};
    };

    struct AssignmentPipelineAllZoneSearchResult final {
        InputModel                          input{};
        VehicleJourneyItemCapacityInput     vehicle_journey_item_capacity{};
        PreprocessedNetwork                 network{};
        AllZoneConnectionSearchResult       search{};
        AssignmentExecutionConfig           execution{};
        SkimMatrixConfig                    skim_config{};
        CapacityAwareAssignmentDiagnostics  capacity_aware{};
    };

    struct AssignmentPipelineTimedDiagnosticsResult final {
        InputModel                          input{};
        VehicleJourneyItemCapacityInput     vehicle_journey_item_capacity{};
        PreprocessedNetwork                 network{};
        ConnectionSearchResult              search{};
        AssignmentExecutionConfig           execution{};
        SkimMatrixConfig                    skim_config{};
        CapacityAwareAssignmentDiagnostics  capacity_aware{};
    };

    using AssignmentPipelineResult = std::variant<
          AssignmentPipelineCalculatedResult
        , AssignmentPipelineOdDayCalculatedResult
        , AssignmentPipelineAllZoneSearchResult
        , AssignmentPipelineTimedDiagnosticsResult
        , AssignmentPipelineDisabledResult
    >;

    /**
     * @brief Run the timetable assignment pipeline according to execution config.
     *
     * Calculated mode order:
     * preprocessing -> connection search -> connection choice -> demand split.
     */
    mathfp::Expected<AssignmentPipelineResult> run_timetable_assignment_pipeline(
        AssignmentInput input
    );

}  // namespace timetable::domain::assignment
