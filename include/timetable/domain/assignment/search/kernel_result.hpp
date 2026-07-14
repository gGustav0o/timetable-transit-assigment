#pragma once

#include <vector>

#include "timetable/domain/assignment/complete_connection_metrics.hpp"
#include "timetable/domain/assignment/search/connection.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Complete connection emitted by the branch-and-bound kernel.
     *
     * This candidate is neutral with respect to demand tasks, OD-day grouping,
     * final choice and output projections.
     */
    struct CompleteConnectionCandidate final {
        SearchConnection          connection;
        CompleteConnectionMetrics metrics{};
    };

    struct SearchKernelDiagnostics final {
        std::size_t completed_connection_count{};
        std::size_t retained_connection_count{};
        std::size_t pruned_branch_count{};
    };

    struct SearchKernelResult final {
        ZoneId                                   origin;
        std::vector<CompleteConnectionCandidate> candidates{};
        SearchKernelDiagnostics                 diagnostics{};
    };

}  // namespace timetable::domain::assignment
