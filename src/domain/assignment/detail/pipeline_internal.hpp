#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/pipeline.hpp"

namespace timetable::domain::assignment::detail {

    // Private bridge used only by pipeline.cpp. Application code must depend on
    // the public run_timetable_assignment_pipeline contract instead.
    mathfp::Expected<AssignmentPipelineResult> run_timetable_assignment_pipeline_with_context(
        AssignmentInput input
    );

}  // namespace timetable::domain::assignment::detail
