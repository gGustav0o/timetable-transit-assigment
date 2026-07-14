#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/od_day_path_search.hpp"

namespace timetable::domain::assignment::runtime {

    mathfp::Expected<mathfp::Unit> search_od_day_paths_by_origin_branch_and_bound(
        OdDayPathOriginSearchRequest request
    );

}  // namespace timetable::domain::assignment::runtime
