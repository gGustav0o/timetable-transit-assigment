#pragma once

#include <span>
#include <vector>

#include "timetable/domain/assignment/day_path/retention.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] DayPathMetrics day_path_metrics_of(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] std::vector<SearchConnection> finalize_day_path_representatives(
        DayPathRetention retention
    );

    [[nodiscard]] std::vector<DayPathAlternative> finalize_day_path_alternatives(
        DayPathRetention retention
    );

    [[nodiscard]] CompleteConnectionMetricSummary summarize_day_path_metrics(
        const DayPathRetention& retention
    ) noexcept;

    [[nodiscard]] std::vector<DayPathAlternative> finalize_day_path_alternatives(
          DayPathRetention        retention
        , const ChoiceTolerances& tolerances
        , ChoiceRolloutStage      rollout_stage
    );

    [[nodiscard]] std::vector<SearchConnection> finalize_day_path_representatives(
          DayPathRetention        retention
        , const ChoiceTolerances& tolerances
        , ChoiceRolloutStage      rollout_stage
    );

    [[nodiscard]] std::vector<SearchConnection> day_path_representative_connections(
        std::span<const DayPathAlternative> alternatives
    );

}  // namespace timetable::domain::assignment
