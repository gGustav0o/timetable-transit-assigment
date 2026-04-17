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

}  // namespace timetable::infra::csv
