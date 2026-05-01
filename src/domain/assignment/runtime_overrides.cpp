#include "timetable/domain/assignment/runtime_overrides.hpp"

#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/capacity_aware_assignment.hpp"
#include "timetable/domain/assignment/execution_config.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] CapacityAwareSearchMode disabled_behavioral_search_mode(
            const SearchParams& params
        ) noexcept {
            return mathfp::units::as_dimless(
                params.impedance.volume_capacity_ratio
            ) > 0.0
                ? CapacityAwareSearchMode::StoredOnly
                : CapacityAwareSearchMode::Disabled;
        }

        void force_disable_behavioral_capacity_aware_assignment(
            AssignmentInput& input
        ) noexcept {
            input.capacity_aware_assignment.capacity_aware_split_enabled = false;
            input.capacity_aware_assignment.search_mode =
                disabled_behavioral_search_mode(input.params);
            input.params.split.perceived_journey_time.volume_capacity_ratio =
                Dimless{};
        }

        void apply_capacity_stage_overrides(
              AssignmentInput& input
            , const timetable::config::CapacityStageOverridePolicy& policy
        ) noexcept {
            if (policy.allow_behavioral_capacity_aware_assignment.has_value()
                && !*policy.allow_behavioral_capacity_aware_assignment) {
                force_disable_behavioral_capacity_aware_assignment(input);
            }

            if (policy.calculate_vehicle_journey_item_overload_assessment.has_value()) {
                input.execution.calculate_vehicle_journey_item_overload_assessment =
                    *policy.calculate_vehicle_journey_item_overload_assessment;
            }
        }

    }  // namespace

    mathfp::Expected<AssignmentInput> apply_runtime_override_policy(
          AssignmentInput                          input
        , const timetable::config::RuntimeOverridePolicy& policy
    ) {
        if (!policy.enabled) {
            return input;
        }

        apply_capacity_stage_overrides(input, policy.capacity);

        MATHFP_TRY(validate_assignment_execution_config(input.execution));
        MATHFP_TRY(validate_capacity_aware_assignment_config(
            input.capacity_aware_assignment
        ));
        return input;
    }

}  // namespace timetable::domain::assignment
