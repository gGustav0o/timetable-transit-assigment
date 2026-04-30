#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
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
          const InputModel&               input
        , const PreprocessedNetwork&    network
        , const ConnectionSearchResult& search_result
        , const ConnectionChoiceResult& choice_result
        , const DemandSplitResult&      split_result
        , const SkimMatrixConfig&       skim_config
    );

    /**
     * @brief Build a canonical output for the mode where assignment is disabled.
     *
     * The output preserves the OD-time demand structure and contains no
     * calculated connections, shares, loads, or skim entries.
     */
    mathfp::Expected<AssignmentOutput> build_assignment_disabled_output(
          const InputModel&       input
        , const SkimMatrixConfig& skim_config
    );

}  // namespace timetable::domain::assignment
