#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Convert accumulated assignment stage results to the public output.
     */
    mathfp::Expected<AssignmentOutput> build_assignment_output(
        const InputModel& input
        , const PreprocessedNetwork& network
        , const ConnectionSearchResult& search_result
        , const ConnectionChoiceResult& choice_result
        , const DemandSplitResult& split_result
    );

}  // namespace timetable::domain::assignment
