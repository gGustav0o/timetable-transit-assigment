#include "detail/output_internal.hpp"

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment::detail {

    mathfp::Expected<const TimeInterval*> find_interval(
          const InputModel& input
        , IntervalId        interval_id
    ) {
        for (const auto& interval : input.intervals) {
            if (interval.id == interval_id) {
                return &interval;
            }
        }

        return mathfp::unexpected(
            mathfp::internal_error("assignment output mapping references unknown interval")
                .ctx("interval_id", interval_id.get())
        );
    }

}  // namespace timetable::domain::assignment::detail
