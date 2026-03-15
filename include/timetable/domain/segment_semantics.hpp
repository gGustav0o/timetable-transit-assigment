#pragma once

#include "timetable/domain/segments.hpp"

namespace timetable::domain {

    [[nodiscard]] constexpr CarrierKind carrier_kind(const SegmentCarrier& carrier) noexcept {
        return std::holds_alternative<LineId>(carrier) ? CarrierKind::Line : CarrierKind::Walk;
    }

    [[nodiscard]] constexpr bool is_line(const SegmentCarrier& carrier) noexcept {
        return carrier_kind(carrier) == CarrierKind::Line;
    }

    [[nodiscard]] constexpr bool is_walk(const SegmentCarrier& carrier) noexcept {
        return carrier_kind(carrier) == CarrierKind::Walk;
    }

    [[nodiscard]] inline bool has_trip(const ConnectionSegment& segment) noexcept {
        return segment.trip.has_value();
    }

    [[nodiscard]] inline bool has_times(const ConnectionSegment& segment) noexcept {
        return segment.departure.has_value();
    }

    [[nodiscard]] inline bool is_timed_connection_segment(
        const ConnectionSegment& segment
    ) noexcept {
        return has_times(segment);
    }

    [[nodiscard]] inline bool is_walk_connection_segment(
        const ConnectionSegment& segment
    ) noexcept {
        return !is_timed_connection_segment(segment);
    }

    [[nodiscard]] inline bool route_and_connection_kinds_match(
        const RouteSegment& route_segment
        , const ConnectionSegment& connection_segment
    ) noexcept {
        return is_line(route_segment.carrier) == is_timed_connection_segment(connection_segment);
    }

    [[nodiscard]] inline bool same_trip(
        const ConnectionSegment& lhs
        , const ConnectionSegment& rhs
    ) noexcept {
        return lhs.trip.has_value()
            && rhs.trip.has_value()
            && lhs.trip.value() == rhs.trip.value();
    }

    [[nodiscard]] inline bool forbids_transfer_to_same_trip(
        const ConnectionSegment& current
        , const ConnectionSegment& successor
    ) noexcept {
        return same_trip(current, successor);
    }

}  // namespace timetable::domain
