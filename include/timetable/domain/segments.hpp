#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/id.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain {

    struct RouteSegmentIdTag {};
    struct ConnectionSegmentIdTag {};

    using RouteSegmentId      = DomainId<RouteSegmentIdTag>;
    using ConnectionSegmentId = DomainId<ConnectionSegmentIdTag>;

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
        WalkEndpoint from;
        WalkEndpoint to;
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
        StopOccurrence from;
        StopOccurrence to;
        LineId         line;
        RouteId        route;
    };

    using RouteTopology = std::variant<WalkRouteTopology, LineRouteTopology>;

    /**
     * @brief Infrastructure-level segment between two nodes.
     *
     * Encodes either:
     * - physical walk topology, or
     * - occurrence-aware timetable line topology.
     */
    //tex:
    // Source correspondence. The preprocessing route-segment object is the route-segment pseudo-record
    // $$y=(i,j,\ell,\tau,\sigma).$$
    // Here initial/terminal nodes are stored in the topology variant, $$\ell$$ is length,
    // $$\tau$$ is run time, and $$\sigma$$ is either a transit line/route reference
    // or the shortest walk-link sequence. This code keeps the same information but makes
    // the variant explicit through WalkRouteTopology/LineRouteTopology.
    struct RouteSegment final {
        RouteSegmentId id;
        Length         length{};
        Time           run_time{};
        RouteTopology  topology;
    };

    /**
     * @brief Timetable-level segment with concrete times (or always-available walk).
     */
    //tex:
    // Connection segment. A timed ride connection is a route segment
    // instantiated by a concrete trip and therefore carries
    // $$t_{\mathrm{dep}}(s),t_{\mathrm{arr}}(s).$$
    // A walk connection keeps the same route-segment reference but has no fixed
    // departure or arrival instant because it is available for every traveller time.
    struct ConnectionSegment final {
        ConnectionSegmentId          id;
        RouteSegmentId               route_segment;
        std::optional<TripId>        trip{};
        std::optional<RoutePosition> from_index{};
        std::optional<RoutePosition> to_index{};
        std::optional<Time>          departure{};
        std::optional<Time>          arrival{};
        std::optional<double>        fare{};
    };

}  // namespace timetable::domain
