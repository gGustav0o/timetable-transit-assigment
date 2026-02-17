#pragma once

#include "timetable/domain/segments.hpp"

namespace timetable::domain {

    [[nodiscard]] inline bool route_segment_less(
        const RouteSegment& a
        , const RouteSegment& b
    ) {
        if (const auto a_from = to_endpoint_key(a.from), b_from = to_endpoint_key(b.from); a_from != b_from)
            return a_from < b_from;

        if (const auto a_to = to_endpoint_key(a.to), b_to = to_endpoint_key(b.to); a_to != b_to)
            return a_to < b_to;

        if (const auto a_kind = carrier_kind(a.carrier), b_kind = carrier_kind(b.carrier); a_kind != b_kind)
            return a_kind < b_kind;

        if (const auto a_carrier = is_line(a.carrier) ? std::get<LineId>(a.carrier).get() : 0
            , b_carrier = is_line(b.carrier) ? std::get<LineId>(b.carrier).get() : 0
            ; a_carrier != b_carrier
        ) return a_carrier < b_carrier;

        return a.id.get() < b.id.get();
    }

}  // namespace timetable::domain
