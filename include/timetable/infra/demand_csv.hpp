#pragma once

#include <filesystem>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"

namespace timetable::infra::csv {

    mathfp::Expected<std::vector<timetable::domain::TimeInterval>> parse_time_intervals_csv(
        const std::filesystem::path& path
    );

    mathfp::Expected<std::vector<timetable::domain::DemandEntry>> parse_od_demand_csv(
          const std::filesystem::path&                        path
        , const std::vector<timetable::domain::TimeInterval>& intervals
    );

    mathfp::Expected<std::vector<timetable::domain::DemandEntry>> parse_daily_od_matrix_xlsx(
          const std::filesystem::path&       path
        , timetable::domain::IntervalId      interval
    );

}  // namespace timetable::infra::csv
