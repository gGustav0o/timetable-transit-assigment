#include "timetable/domain/assignment/output.hpp"

#include "detail/output_internal.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<AssignmentOutput> build_assignment_output(
          const InputModel&             input
        , const PreprocessedNetwork&    network
        , const ConnectionSearchResult& search_result
        , const ConnectionChoiceResult& choice_result
        , const DemandSplitResult&      split_result
        , const SkimMatrixConfig&       skim_config
    ) {
        return detail::build_assignment_output_impl(
              input
            , network
            , search_result
            , choice_result
            , split_result
            , skim_config
        );
    }

    mathfp::Expected<AssignmentOutput> build_assignment_disabled_output(
          const InputModel&       input
        , const SkimMatrixConfig& skim_config
    ) {
        return detail::build_assignment_disabled_output_impl(input, skim_config);
    }

}  // namespace timetable::domain::assignment
