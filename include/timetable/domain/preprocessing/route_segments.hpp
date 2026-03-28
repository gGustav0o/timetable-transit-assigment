#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::preprocessing {

    /**
     * @brief Build line-based route segments from routes and trips.
     *
     * For each route, creates segments for all ordered stop-occurrence pairs
     * i<j along the route order.
     *
     * Length policy:
     *  - If line_speed is provided, length = line_speed * running_time.
     *  - Otherwise length defaults to 0 (no length data available).
     */
    mathfp::Expected<std::vector<RouteSegment>> build_line_route_segments(
        const std::vector<Route>& routes
        , const std::vector<Trip>& trips
        , const std::vector<Stop>& stops
        , const PreprocessParams& params
    );

     /**
      * @brief Build walk route segments via shortest paths over walk links.
      *
     * If deduplicate_walk_segments is enabled, duplicate (from,to,path) walk paths
     * are removed in a stable manner.
     */
    mathfp::Expected<std::vector<RouteSegment>> build_walk_route_segments(
        const std::vector<WalkLink>& walk_links
        , const PreprocessParams& params
    );

}  // namespace timetable::domain::preprocessing
