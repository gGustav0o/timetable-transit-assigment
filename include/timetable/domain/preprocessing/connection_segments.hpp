#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::preprocessing {

    /**
     * @brief Build timetable-based connection segments from route segments and trips.
     *
     * For line route segments, each trip yields a timed connection segment.
     * Timed segment fare is derived from the corresponding raw line fare;
     * lines used by timed topology must therefore carry explicit fare metadata.
     * Walk route segments yield time-independent connection segments.
     *
     * ConnectionSegmentId assignment is deterministic when stable_ordering is enabled;
     * it follows the sorted route_segments order and per-line trip order.
     */
    mathfp::Expected<std::vector<ConnectionSegment>> build_connection_segments(
          const std::vector<RouteSegment>& route_segments
        , const std::vector<Line>&       lines
        , const std::vector<Route>&      routes
        , const std::vector<Trip>&       trips
        , const PreprocessParams&        params
    );

}  // namespace timetable::domain::preprocessing
