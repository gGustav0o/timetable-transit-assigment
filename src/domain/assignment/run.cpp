#include "timetable/domain/assignment/run.hpp"

#include <utility>

#include <mathfp/core/fp.hpp>

#include "detail/pipeline_internal.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<AssignmentOutput> run_timetable_assignment(
        AssignmentInput input
    ) {
        return
            detail::run_timetable_assignment_pipeline_with_context(std::move(input))
            | mathfp::fp::pipe::and_then([](const detail::AssignmentPipelineResult& result) {
                return build_assignment_output(
                    result.input
                    , result.network
                    , result.search
                    , result.choice
                    , result.split
                );
            });
    }

}  // namespace timetable::domain::assignment
