#pragma once

#include <cstdint>

#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/problem.hpp"

namespace timetable::domain::assignment::test_support {

    inline SearchTimeDomain domain(double begin, double end) {
        return SearchTimeDomain{
            .windows = {
                SearchTimeWindow{
                      .begin = Time{ begin }
                    , .end   = Time{ end }
                }
            }
        };
    }

    inline TimeInterval interval(std::int64_t id, double start, double end) {
        return TimeInterval{
              .id    = IntervalId{ id }
            , .start = Time{ start }
            , .end   = Time{ end }
        };
    }

    inline SearchTask task(
          std::int64_t index
        , std::int64_t origin
        , std::int64_t destination
        , std::int64_t interval_id
        , double       window_begin
        , double       window_end
    ) {
        return SearchTask{
              .index            = SearchTaskRef{ index }
            , .origin           = ZoneId{ origin }
            , .destination      = ZoneId{ destination }
            , .interval         = interval(interval_id, window_begin, window_end)
            , .departure_domain = domain(window_begin, window_end)
        };
    }

    inline SearchCompletionTarget completion_target(
          std::int64_t index
        , std::int64_t destination
    ) {
        return SearchCompletionTarget{
              .index       = SearchCompletionTargetRef{ index }
            , .destination = ZoneId{ destination }
        };
    }

}  // namespace timetable::domain::assignment::test_support
