#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/infra/segment_columns.hpp"

namespace timetable::infra {

    struct PresegmentedInputBuildParams final {
        bool allow_unknown_zones{ false };
    };

    /**
     * @brief Build AssignmentInput from already parsed pre-segmented columns.
     *
     * This is the common bridge used by format-specific parsers such as the
     * single-file TXT reader and the pair CSV reader.
     */
    mathfp::Expected<timetable::domain::AssignmentInput> build_default_presegmented_assignment_input(
        SegmentColumns columns
    );

    mathfp::Expected<timetable::domain::AssignmentInput> build_presegmented_assignment_input(
          SegmentColumns                      columns
        , const PresegmentedInputBuildParams& params
    );

}  // namespace timetable::infra
