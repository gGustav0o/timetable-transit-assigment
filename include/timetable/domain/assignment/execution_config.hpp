#pragma once

#include <array>
#include <optional>
#include <string_view>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"

namespace timetable::domain::assignment {

    enum class AssignmentOutputExportProfile {
          ProductionAggregate
        , DiagnosticFullPath
    };

    inline constexpr std::array kAssignmentOutputExportProfileTokens{
          timetable::EnumStringEntry<AssignmentOutputExportProfile>{
              AssignmentOutputExportProfile::ProductionAggregate,
              "production_aggregate"
          }
        , timetable::EnumStringEntry<AssignmentOutputExportProfile>{
              AssignmentOutputExportProfile::DiagnosticFullPath,
              "diagnostic_full_path"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        AssignmentOutputExportProfile value
    ) noexcept {
        return timetable::enum_to_string(value, kAssignmentOutputExportProfileTokens);
    }

    [[nodiscard]] inline constexpr std::optional<AssignmentOutputExportProfile>
    assignment_output_export_profile_from_string(std::string_view token) noexcept {
        return timetable::enum_from_string(token, kAssignmentOutputExportProfileTokens);
    }

    /**
     * @brief Runtime execution switches for the assignment pipeline.
     *
     * This configuration controls which top-level calculation stages are
     * executed. It is intentionally separate from SearchParams because it does
     * not define impedance, choice or split mathematics.
     */
    struct AssignmentExecutionConfig final {
        bool calculate_assignment{ true };
        bool calculate_vehicle_journey_item_overload_assessment{ true };
        AssignmentOutputExportProfile output_export_profile{
            AssignmentOutputExportProfile::ProductionAggregate
        };
    };

    [[nodiscard]] constexpr std::string_view assignment_output_export_profile_name(
        AssignmentOutputExportProfile profile
    ) noexcept {
        return to_string(profile);
    }

    mathfp::Expected<mathfp::Unit> validate_assignment_execution_config(
        const AssignmentExecutionConfig& config
    );

}  // namespace timetable::domain::assignment
