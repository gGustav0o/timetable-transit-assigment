#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain {

    struct RouteSegmentIdTag {};
    struct ConnectionSegmentIdTag {};

    using RouteSegmentId = mathfp::StrongType<
        std::int64_t
        , RouteSegmentIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using ConnectionSegmentId = mathfp::StrongType<
        std::int64_t
        , ConnectionSegmentIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    enum class RouteTopologyKind : std::uint8_t {
          Line
        , Walk
    };

    using WalkPath = std::vector<WalkLinkId>;

    /**
     * @brief Physical walk topology.
     *
     * Walk segments live in the physical network space (stops/zones).
     */
    struct WalkRouteTopology final {
        WalkEndpoint from{};
        WalkEndpoint to{};
        WalkPath     path{};
    };

    /**
     * @brief Timetable line topology.
     *
     * Line segments live in the route-occurrence space and therefore distinguish
     * route patterns and repeated appearances of the same physical stop on loop
     * lines.
     */
    struct LineRouteTopology final {
        StopOccurrence from{};
        StopOccurrence to{};
        LineId         line{};
        RouteId        route{};
    };

    using RouteTopology = std::variant<WalkRouteTopology, LineRouteTopology>;

    /**
     * @brief Infrastructure-level segment between two nodes.
     *
     * Encodes either:
     * - physical walk topology, or
     * - occurrence-aware timetable line topology.
     */
    struct RouteSegment final {
        RouteSegmentId id{};
        Length         length{};
        Time           run_time{};
        RouteTopology  topology{};
    };

    /**
     * @brief Timetable-level segment with concrete times (or always-available walk).
     */
    struct ConnectionSegment final {
        ConnectionSegmentId          id{};
        RouteSegmentId               route_segment{};
        std::optional<TripId>        trip{};
        std::optional<RoutePosition> from_index{};
        std::optional<RoutePosition> to_index{};
        std::optional<Time>          departure{};
        std::optional<Time>          arrival{};
        std::optional<double>        fare{};
    };

}  // namespace timetable::domain
