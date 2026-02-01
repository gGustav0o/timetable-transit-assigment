#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/fp.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/steps.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Run the full timetable assignment pipeline.
     *
     * Order: preprocessing -> connection search -> connection choice -> demand split.
     */
    inline mathfp::Expected<DemandSplitResult> run_timetable_assignment_pipeline(
        const AssignmentInput& input
    ) {
        using mathfp::fp::pipe::and_then;
        return
            build_preprocessed_network(input.input, input.params.preprocess)
            | and_then([&](PreprocessedNetwork net) {
                return search_connections_branch_and_bound(net, input.params);
            })
            | and_then([&](ConnectionSearchResult search_result) {
                return choose_connections(search_result, input.params);
            })
            | and_then([&](ConnectionChoiceResult choice_result) {
                return split_demand_over_connections(choice_result, input.input, input.params);
            });
    }

}  // namespace timetable::domain::assignment
