#pragma once

#include <cstddef>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/day_path/support.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] bool better_day_path_representative(
          const CompleteConnectionMetrics& candidate
        , const CompleteConnectionMetrics& current
    ) noexcept;

    [[nodiscard]] bool day_path_retention_empty(
        const DayPathRetention& retention
    ) noexcept;

    [[nodiscard]] std::size_t day_path_retention_size(
        const DayPathRetention& retention
    ) noexcept;

    [[nodiscard]] mathfp::Expected<mathfp::Unit> retain_day_path_support(
          DayPathSplitSupport&          support
        , DayPathSupportDescriptor      candidate
        , const DayPathRetentionConfig& config
    );

    [[nodiscard]] CompleteConnectionMetrics best_day_path_support_metrics(
        const DayPathAlternative& alternative
    );

    [[nodiscard]] std::vector<DayPathSignature> sorted_day_path_signatures(
        const DayPathRetention& retention
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> enforce_day_path_retention(
          DayPathRetention&             retention
        , const DayPathRetentionConfig& config
    );

}  // namespace timetable::domain::assignment
