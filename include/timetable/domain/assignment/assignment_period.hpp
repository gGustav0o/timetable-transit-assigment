#pragma once

#include <cstdint>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Input unit used for basePara.preAssignPeriod/postAssignPeriod.
     *
     * The params.txt source is ambiguous in existing examples. The domain
     * model stores normalized Time values; this enum controls only conversion
     * from raw params.txt numeric values.
     */
    enum class AssignmentPeriodUnit : std::uint8_t {
          Minutes
        , Seconds
    };

    inline constexpr AssignmentPeriodUnit kAssignmentPeriodInputUnit =
        AssignmentPeriodUnit::Minutes;

    /**
     * @brief Demand assignment period around an original demand interval.
     *
     * For an interval [start, end], the assignment period is:
     * [start - pre_assign_period, end + post_assign_period].
     *
     * The original demand interval remains the behavioral reference for split
     * temporal utility; this config only defines the admissible assignment-time
     * support around it.
     */
    struct AssignmentPeriodConfig final {
        Time pre_assign_period{};
        Time post_assign_period{};
    };

    [[nodiscard]] Time assignment_period_input_time(
          double               raw_value
        , AssignmentPeriodUnit unit = kAssignmentPeriodInputUnit
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_assignment_period_config(
        const AssignmentPeriodConfig& config
    );

    mathfp::Expected<AssignmentPeriodConfig> make_assignment_period_config(
          double               pre_assign_period
        , double               post_assign_period
        , AssignmentPeriodUnit unit = kAssignmentPeriodInputUnit
    );

    [[nodiscard]] SearchTimePadding assignment_time_padding(
        const AssignmentPeriodConfig& config
    ) noexcept;

}  // namespace timetable::domain::assignment
