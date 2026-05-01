#pragma once

#include <optional>

namespace timetable::config {

    struct CapacityStageOverridePolicy final {
        std::optional<bool> allow_behavioral_capacity_aware_assignment{};
        std::optional<bool> calculate_vehicle_journey_item_overload_assessment{};
    };

    struct RuntimeOverridePolicy final {
        bool                        enabled{ false };
        CapacityStageOverridePolicy capacity{};
    };

#if defined(TIMETABLE_ENABLE_RUNTIME_OVERRIDES)

    inline constexpr bool kRuntimeOverridesEnabled = true;

    inline constexpr RuntimeOverridePolicy kRuntimeOverridePolicy{
          .enabled = true
        , .capacity = CapacityStageOverridePolicy{
              .allow_behavioral_capacity_aware_assignment =
#if defined(TIMETABLE_OVERRIDE_DISABLE_CAPACITY_AWARE_ASSIGNMENT)
                  false
#else
                  std::nullopt
#endif
            , .calculate_vehicle_journey_item_overload_assessment =
#if defined(TIMETABLE_OVERRIDE_DISABLE_VEHICLE_JOURNEY_ITEM_OVERLOAD_ASSESSMENT)
                  false
#else
                  std::nullopt
#endif
          }
    };

#else

    inline constexpr bool kRuntimeOverridesEnabled = false;

    inline constexpr RuntimeOverridePolicy kRuntimeOverridePolicy{};

#endif

}  // namespace timetable::config
