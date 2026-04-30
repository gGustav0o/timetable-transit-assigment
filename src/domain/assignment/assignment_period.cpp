#include "timetable/domain/assignment/assignment_period.hpp"

#include <cmath>
#include <utility>

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] double assignment_period_unit_multiplier(
            AssignmentPeriodUnit unit
        ) noexcept {
            switch (unit) {
                case AssignmentPeriodUnit::Minutes:
                    return 60.0;
                case AssignmentPeriodUnit::Seconds:
                    return 1.0;
            }
            return 1.0;
        }

    }  // namespace

    Time assignment_period_input_time(
          double               raw_value
        , AssignmentPeriodUnit unit
    ) noexcept {
        return Time{ raw_value * assignment_period_unit_multiplier(unit) };
    }

    mathfp::Expected<mathfp::Unit> validate_assignment_period_config(
        const AssignmentPeriodConfig& config
    ) {
        if (!std::isfinite(config.pre_assign_period.value())
            || config.pre_assign_period.value() < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("pre_assign_period must be finite and non-negative")
                    .ctx("pre_assign_period", config.pre_assign_period.value())
            );
        }

        if (!std::isfinite(config.post_assign_period.value())
            || config.post_assign_period.value() < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("post_assign_period must be finite and non-negative")
                    .ctx("post_assign_period", config.post_assign_period.value())
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<AssignmentPeriodConfig> make_assignment_period_config(
          double               pre_assign_period
        , double               post_assign_period
        , AssignmentPeriodUnit unit
    ) {
        AssignmentPeriodConfig config{
              .pre_assign_period  = assignment_period_input_time(pre_assign_period, unit)
            , .post_assign_period = assignment_period_input_time(post_assign_period, unit)
        };

        auto validated = validate_assignment_period_config(config);
        if (!validated) {
            return mathfp::unexpected(std::move(validated.error()));
        }
        return config;
    }

    SearchTimePadding assignment_time_padding(
        const AssignmentPeriodConfig& config
    ) noexcept {
        return SearchTimePadding{
              .before_start = config.pre_assign_period
            , .after_end    = config.post_assign_period
        };
    }

}  // namespace timetable::domain::assignment
