#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/assignment/split/split.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_input(
        const AssignmentInput& input
    );

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_output(
          const PreprocessedNetwork& network
        , const SearchParams&      params
    );

    mathfp::Expected<mathfp::Unit> validate_search_step_output(
          const ConnectionSearchResult& result
        , const PreprocessedNetwork&  network
        , double                      fare_scale
        , const SearchParams&         params
    );

    mathfp::Expected<mathfp::Unit> validate_choice_step_output(
          const ConnectionChoiceResult&   choice_result
        , const ConnectionSearchResult& search_result
    );

    mathfp::Expected<mathfp::Unit> validate_split_step_input(
          const ConnectionChoiceResult& choice_result
        , const InputModel&           input
    );

    mathfp::Expected<mathfp::Unit> validate_split_step_output(
          const DemandSplitResult&        split_result
        , const ConnectionChoiceResult& choice_result
        , const InputModel&             input
    );

}  // namespace timetable::domain::assignment
