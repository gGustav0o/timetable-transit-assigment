#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/od_day_path_contract.hpp"
#include "timetable/domain/assignment/od_day_path_result.hpp"
#include "timetable/domain/assignment/search/request.hpp"

namespace timetable::domain::assignment {

    struct OdDayPathSearchRequest final {
        BranchAndBoundSearchRequest search;
    };

    struct OdDayPathOriginSearchRequest final {
        BranchAndBoundSearchRequest search;
        OdDayOriginResultSink       origin_sink;
    };

    mathfp::Expected<OdDayPathSearchResult> search_od_day_paths_branch_and_bound(
        const OdDayPathSearchRequest& request
    );

    mathfp::Expected<mathfp::Unit> search_od_day_paths_by_origin_branch_and_bound(
        OdDayPathOriginSearchRequest request
    );

    mathfp::Expected<OdDayConnectionSearchResult> search_od_day_connections_branch_and_bound(
        const OdDayPathSearchRequest& request
    );

    mathfp::Expected<mathfp::Unit> search_od_day_connections_by_origin_branch_and_bound(
        OdDayPathOriginSearchRequest request
    );

}  // namespace timetable::domain::assignment
