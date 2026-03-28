#pragma once

#include "timetable/domain/segments.hpp"

namespace timetable::domain {

    [[nodiscard]] constexpr RouteTopologyKind route_topology_kind(const RouteTopology& topology) noexcept {
        return std::holds_alternative<LineRouteTopology>(topology)
            ? RouteTopologyKind::Line
            : RouteTopologyKind::Walk;
    }

    [[nodiscard]] constexpr RouteTopologyKind route_topology_kind(const RouteSegment& segment) noexcept {
        return route_topology_kind(segment.topology);
    }

    [[nodiscard]] constexpr bool is_line(const RouteTopology& topology) noexcept {
        return route_topology_kind(topology) == RouteTopologyKind::Line;
    }

    [[nodiscard]] constexpr bool is_line(const RouteSegment& segment) noexcept {
        return is_line(segment.topology);
    }

    [[nodiscard]] constexpr bool is_walk(const RouteTopology& topology) noexcept {
        return route_topology_kind(topology) == RouteTopologyKind::Walk;
    }

    [[nodiscard]] constexpr bool is_walk(const RouteSegment& segment) noexcept {
        return is_walk(segment.topology);
    }

    [[nodiscard]] inline const WalkRouteTopology* walk_topology_of(
        const RouteSegment& segment
    ) noexcept {
        return std::get_if<WalkRouteTopology>(&segment.topology);
    }

    [[nodiscard]] inline const LineRouteTopology* line_topology_of(
        const RouteSegment& segment
    ) noexcept {
        return std::get_if<LineRouteTopology>(&segment.topology);
    }

    [[nodiscard]] constexpr WalkEndpoint physical_endpoint(
        StopOccurrence occurrence
    ) noexcept {
        return WalkEndpoint{ occurrence.stop };
    }

    [[nodiscard]] inline WalkEndpoint physical_from_endpoint(
        const RouteSegment& segment
    ) noexcept {
        if (const auto* walk = walk_topology_of(segment)) {
            return walk->from;
        }
        return physical_endpoint(line_topology_of(segment)->from);
    }

    [[nodiscard]] inline WalkEndpoint physical_to_endpoint(
        const RouteSegment& segment
    ) noexcept {
        if (const auto* walk = walk_topology_of(segment)) {
            return walk->to;
        }
        return physical_endpoint(line_topology_of(segment)->to);
    }

    [[nodiscard]] inline EndpointKey physical_from_key(
        const RouteSegment& segment
    ) noexcept {
        return to_endpoint_key(physical_from_endpoint(segment));
    }

    [[nodiscard]] inline EndpointKey physical_to_key(
        const RouteSegment& segment
    ) noexcept {
        return to_endpoint_key(physical_to_endpoint(segment));
    }

    [[nodiscard]] inline std::optional<LineId> line_of(
        const RouteSegment& segment
    ) noexcept {
        if (!is_line(segment)) {
            return std::nullopt;
        }
        return line_topology_of(segment)->line;
    }

    [[nodiscard]] inline bool same_line(
        const RouteSegment& lhs
        , const RouteSegment& rhs
    ) noexcept {
        const auto lhs_line = line_of(lhs);
        const auto rhs_line = line_of(rhs);
        return lhs_line.has_value()
            && rhs_line.has_value()
            && lhs_line.value() == rhs_line.value();
    }

    [[nodiscard]] inline bool has_trip_reference(const ConnectionSegment& segment) noexcept {
        return segment.trip.has_value();
    }

    [[nodiscard]] inline bool has_schedule_times(const ConnectionSegment& segment) noexcept {
        return segment.departure.has_value();
    }

    [[nodiscard]] inline bool is_timed_connection(
        const ConnectionSegment& segment
    ) noexcept {
        return has_schedule_times(segment);
    }

    [[nodiscard]] inline bool is_walk_connection(
        const ConnectionSegment& segment
    ) noexcept {
        return !is_timed_connection(segment);
    }

    [[nodiscard]] inline bool route_topology_matches_connection_mode(
        const RouteSegment& route_segment
        , const ConnectionSegment& connection_segment
    ) noexcept {
        return is_line(route_segment) == is_timed_connection(connection_segment);
    }

    [[nodiscard]] inline bool share_trip_reference(
        const ConnectionSegment& lhs
        , const ConnectionSegment& rhs
    ) noexcept {
        return lhs.trip.has_value()
            && rhs.trip.has_value()
            && lhs.trip.value() == rhs.trip.value();
    }

    [[nodiscard]] inline bool transfer_reuses_same_trip(
        const ConnectionSegment& current
        , const ConnectionSegment& successor
    ) noexcept {
        return share_trip_reference(current, successor);
    }

}  // namespace timetable::domain
