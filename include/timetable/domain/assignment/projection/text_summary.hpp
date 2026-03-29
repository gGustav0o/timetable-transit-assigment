#pragma once

#include <cstddef>
#include <string>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/projection/summary.hpp"

namespace timetable::domain::assignment::projection {

    /**
     * @brief Options for the compact human-readable assignment summary.
     */
    struct AssignmentTextSummaryOptions final {
        std::size_t max_od_results         { 5 };
        std::size_t max_connections_per_od { 3 };
        bool        include_empty_ods      { false };
    };

    /**
     * @brief Format a compact human-readable summary from the derived summary view.
     */
    std::string format_assignment_result_summary(
          const AssignmentResultSummary&        summary
        , const AssignmentTextSummaryOptions& options = {}
    );

    /**
     * @brief Build and format a compact human-readable summary from the canonical result.
     */
    mathfp::Expected<std::string> format_assignment_output_summary(
          const AssignmentOutput&               output
        , const AssignmentTextSummaryOptions& options = {}
    );

}  // namespace timetable::domain::assignment::projection
