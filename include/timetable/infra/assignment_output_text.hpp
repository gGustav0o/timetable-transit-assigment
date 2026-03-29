#pragma once

#include <filesystem>
#include <string>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/projection/text_summary.hpp"

namespace timetable::infra {

    mathfp::Expected<std::string> serialize_assignment_output_summary_text(
          const timetable::domain::AssignmentOutput&                                     output
        , const timetable::domain::assignment::projection::AssignmentTextSummaryOptions& options = {}
    );

    mathfp::Expected<mathfp::Unit> write_assignment_output_summary_text(
          const timetable::domain::AssignmentOutput&                                     output
        , const std::filesystem::path&                                                   path
        , const timetable::domain::assignment::projection::AssignmentTextSummaryOptions& options = {}
    );

}  // namespace timetable::infra
