#include "pipeline_steps.hpp"

#include <algorithm>

#include <mathfp/types/units.hpp>

namespace timetable::domain::assignment::detail {

    bool capacity_aware_search_enabled(
        const AssignmentInput& input
    ) noexcept {
        return input.capacity_aware_assignment.search_mode
            == CapacityAwareSearchMode::Enabled;
    }

    bool capacity_aware_split_enabled(
        const AssignmentInput& input
    ) noexcept {
        return input.capacity_aware_assignment.capacity_aware_split_enabled
            && mathfp::units::as_dimless(
                input.params.split.perceived_journey_time.volume_capacity_ratio
            ) > 0.0;
    }

    Dimless capacity_aware_assignment_used_factor(
        const AssignmentInput& input
    ) noexcept {
        return Dimless{
            std::max(
                  mathfp::units::as_dimless(input.params.impedance.volume_capacity_ratio)
                , mathfp::units::as_dimless(
                      input.params.split.perceived_journey_time.volume_capacity_ratio
                  )
            )
        };
    }


}  // namespace timetable::domain::assignment::detail
