#pragma once

#include <cstddef>
#include <cstdint>

namespace timetable::domain::assignment {

    /**
     * @brief Runtime diagnostics context for search logging.
     *
     * This describes the outer execution run, not the mathematical cost
     * function optimized by branch-and-bound.
     */
    struct SearchDiagnosticsContext final {
        std::int32_t capacity_iteration{};
        std::size_t  declared_zone_count{};
        bool         validate_phase_invariants{};
        bool         log_projection_details{};
    };

}  // namespace timetable::domain::assignment
