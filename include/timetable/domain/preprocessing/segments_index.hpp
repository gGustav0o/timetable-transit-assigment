#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/segments.hpp"

namespace timetable::domain::preprocessing {

    /**
     * @brief Sorted index over route segments for fast access by origin endpoint.
     *
     * Sorting key (lexicographic):
     *  1) from EndpointKey
     *  2) to EndpointKey
     *  3) carrier_kind (Line < Walk)
     *  4) carrier_id (LineId.get(), Walk -> 0)
     *  5) RouteSegmentId
     *
     * Buckets:
     *  - buckets are unique 'from' keys in ascending order.
     *  - offsets.size() == buckets.size() + 1, offsets.back() == order.size().
     */
    struct RouteSegmentIndex final {
        std::vector<RouteSegmentId> order{};
        std::vector<EndpointKey>    buckets{};
        std::vector<std::size_t>    offsets{};
    };

    /**
     * @brief Sorted index over connection segments, split into timed and walk groups.
     *
     * Timed sorting key (lexicographic):
     *  1) from EndpointKey (of underlying RouteSegment)
     *  2) departure time
     *  3) arrival time
     *  4) to EndpointKey
     *  5) RouteSegmentId
     *  6) ConnectionSegmentId
     *
     * Walk sorting key (lexicographic):
     *  1) from EndpointKey
     *  2) to EndpointKey
     *  3) RouteSegmentId
     *  4) ConnectionSegmentId
     *
     * Buckets:
     *  - buckets are unique 'from' keys in ascending order.
     *  - offsets.size() == buckets.size() + 1, offsets.back() == order.size().
     */
    struct ConnectionSegmentIndex final {
        std::vector<ConnectionSegmentId> timed_order{};
        std::vector<Time>                timed_departures{};
        std::vector<EndpointKey>         timed_buckets{};
        std::vector<std::size_t>         timed_offsets{};

        std::vector<ConnectionSegmentId> walk_order{};
        std::vector<EndpointKey>         walk_buckets{};
        std::vector<std::size_t>         walk_offsets{};
    };

    /**
     * @brief Aggregate lookup ranges for segments departing from a given endpoint.
     */
    struct SegmentLookup final {
        std::span<const RouteSegmentId>      route{};
        std::span<const ConnectionSegmentId> timed_connections{};
        std::span<const ConnectionSegmentId> walk_connections{};
    };

    /**
     * @brief Build a sorted route segment index according to the RouteSegmentIndex key.
     */
    mathfp::Expected<RouteSegmentIndex> build_route_segment_index(
        std::span<const RouteSegment> segments
    );

    /**
     * @brief Build sorted connection segment indices (timed + walk groups).
     */
    mathfp::Expected<ConnectionSegmentIndex> build_connection_segment_index(
        std::span<const ConnectionSegment> segments
        , std::span<const RouteSegment> route_segments
    );

    /**
     * @brief Find bucket index for an endpoint key (if present).
     */
    std::optional<std::size_t> find_bucket(
        std::span<const EndpointKey> buckets
        , const EndpointKey& key
    );

    /**
     * @brief Lookup all segment ranges by origin endpoint.
     */
    SegmentLookup lookup_from(
        const RouteSegmentIndex& route_index
        , const ConnectionSegmentIndex& connection_index
        , EndpointKey from
    );

    /**
     * @brief Find the next timed connection segment from an endpoint at or after a time.
     */
    std::optional<ConnectionSegmentId> next_connection_from(
        const ConnectionSegmentIndex& connection_index
        , EndpointKey from
        , Time time
    );

}  // namespace timetable::domain::preprocessing
