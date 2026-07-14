#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/all_zone_result.hpp"
#include "timetable/domain/assignment/search/connection.hpp"
#include "timetable/domain/assignment/search/request.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Enumerate feasible demand-task connections.
     *
     * This is a public use-case API over the mathematical branch-and-bound
     * kernel declared in `search/kernel.hpp`.
     */
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const BranchAndBoundSearchRequest& request
    );

    mathfp::Expected<AllZoneConnectionSearchResult> search_all_zone_connections_branch_and_bound(
        const BranchAndBoundSearchRequest& request
    );

}  // namespace timetable::domain::assignment
