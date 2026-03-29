#pragma once

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain {

    [[nodiscard]] inline bool route_segment_less(
          const RouteSegment& a
        , const RouteSegment& b
    ) {
        if (const auto a_from = physical_from_key(a), b_from = physical_from_key(b); a_from != b_from)
            return a_from < b_from;

        if (const auto a_to = physical_to_key(a), b_to = physical_to_key(b); a_to != b_to)
            return a_to < b_to;

        if (const auto a_kind = route_topology_kind(a), b_kind = route_topology_kind(b); a_kind != b_kind)
            return a_kind < b_kind;

        if (const auto a_line = line_of(a).has_value() ? line_of(a)->get() : 0
            ,          b_line = line_of(b).has_value() ? line_of(b)->get() : 0
            ; a_line != b_line
        ) return a_line < b_line;

        return a.id.get() < b.id.get();
    }

}  // namespace timetable::domain
