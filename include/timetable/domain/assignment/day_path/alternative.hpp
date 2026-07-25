#pragma once

#include <cstddef>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/day_path/support.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_day_path_alternative(
          const DayPathAlternative& alternative
        , std::size_t               alternative_index
    );

    [[nodiscard]] DayPathAlternative make_day_path_alternative_from_metrics(
          SearchConnection          connection
        , DayPathSignature          signature
        , CompleteConnectionMetrics complete_metrics
        , ConnectionMetrics         connection_metrics
    );

}  // namespace timetable::domain::assignment
