#pragma once

#include <variant>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/skim_config.hpp"
#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment {
    struct AssignmentPipelineDisabledResult final {
        InputModel                          input{};
        VehicleJourneyItemCapacityInput     vehicle_journey_item_capacity{};
        SkimMatrixConfig                    skim_config{};
    };

    struct AssignmentPipelineCalculatedResult final {
        InputModel                          input{};
        VehicleJourneyItemCapacityInput     vehicle_journey_item_capacity{};
        PreprocessedNetwork                 network{};
        ConnectionSearchResult              search{};
        ConnectionChoiceResult              choice{};
        DemandSplitResult                   split{};
        SkimMatrixConfig                    skim_config{};
    };

    using AssignmentPipelineResult = std::variant<
          AssignmentPipelineCalculatedResult
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
