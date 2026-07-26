#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/segments.hpp"

namespace timetable::domain::preprocessing {

    /**
     * @brief Sorted indices over route segments split by topology space.
     *
     * Line route segments are indexed in occurrence space.
     * Walk route segments are indexed in physical endpoint space.
     */
    struct RouteSegmentIndex final {
        std::vector<RouteSegmentId>    line_order{};
        std::vector<StopOccurrenceKey> line_buckets{};
        std::vector<std::size_t>       line_offsets{};

        std::vector<RouteSegmentId>    walk_order{};
        std::vector<EndpointKey>       walk_buckets{};
        std::vector<std::size_t>       walk_offsets{};
    };

    /**
     * @brief Sorted index over connection segments, split into timed and walk groups.
     *
     * Timed sorting key (lexicographic):
     *  1) from StopOccurrenceKey (of underlying RouteSegment)
     *  2) departure time
     *  3) arrival time
     *  4) to StopOccurrenceKey
     *  5) TripId (empty < present)
     *  6) from_index (empty < present)
     *  7) to_index (empty < present)
     *  8) RouteSegmentId
     *  9) ConnectionSegmentId
     *
     * Walk sorting key (lexicographic):
     *  1) from EndpointKey
     *  2) to EndpointKey
     *  3) RouteSegmentId
     *  4) ConnectionSegmentId
     *
     * Boarding sorting key (lexicographic):
     *  1) from StopId (physical stop space)
     *  2) departure time
     *  3) arrival time
     *  4) from StopOccurrenceKey
     *  5) RouteSegmentId
     *  6) ConnectionSegmentId
     *
     * Buckets:
     *  - buckets are unique 'from' keys in ascending order.
     *  - offsets.size() == buckets.size() + 1, offsets.back() == order.size().
     */
    //tex:
    // This is the sorted connection-segment array plus node-local access
    // structures. Timed connections are ordered inside each origin bucket by
    // $$t_{\mathrm{dep}}$$, which makes the operation
    // $$\min\{s: \operatorname{from}(s)=y,\ t_{\mathrm{dep}}(s)\ge t\}$$
    // a binary search. Walk buckets are keyed only by physical origin because
    // walk segments are always available and do not need a time key.
    struct ConnectionSegmentIndex final {
        std::vector<ConnectionSegmentId> timed_order{};
        std::vector<Time>                timed_departures{};
        std::vector<StopOccurrenceKey>   timed_buckets{};
        std::vector<std::size_t>         timed_offsets{};

        std::vector<ConnectionSegmentId> boarding_order{};
        std::vector<Time>                boarding_departures{};
        std::vector<StopId>              boarding_stop_buckets{};
        std::vector<std::size_t>         boarding_offsets{};

        std::vector<ConnectionSegmentId> walk_order{};
        std::vector<EndpointKey>         walk_buckets{};
        std::vector<std::size_t>         walk_offsets{};

        std::vector<ConnectionSegmentId> access_walk_order{};
        std::vector<EndpointKey>         access_walk_buckets{};
        std::vector<std::size_t>         access_walk_offsets{};

        std::vector<ConnectionSegmentId> transfer_walk_order{};
        std::vector<EndpointKey>         transfer_walk_buckets{};
        std::vector<std::size_t>         transfer_walk_offsets{};

        std::vector<ConnectionSegmentId> egress_walk_order{};
        std::vector<EndpointKey>         egress_walk_buckets{};
        std::vector<std::size_t>         egress_walk_offsets{};
    };

    /**
     * @brief Aggregate lookup ranges in the physical and occurrence spaces.
     *
     * `walk_*` ranges are keyed by the physical origin endpoint.
     * `line_route` and `timed_connections` ranges are keyed by an exact
     * origin stop occurrence.
     */
    //tex:
    // A lookup result represents all connection segments outgoing from a fixed
    // node. In occurrence space it exposes timed candidates from the exact
    // stop occurrence; in physical space it exposes access, transfer and egress
    // walk candidates from the same physical endpoint.
    struct SegmentLookup final {
        std::span<const RouteSegmentId>      line_route{};
        std::span<const RouteSegmentId>      walk_route{};
        std::span<const ConnectionSegmentId> timed_connections{};
        std::span<const ConnectionSegmentId> walk_connections{};
        std::span<const ConnectionSegmentId> access_walk_connections{};
        std::span<const ConnectionSegmentId> transfer_walk_connections{};
        std::span<const ConnectionSegmentId> egress_walk_connections{};
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
        , std::span<const RouteSegment>    route_segments
    );

    /**
     * @brief Find bucket index for a physical endpoint key (if present).
     */
    std::optional<std::size_t> find_bucket(
          std::span<const EndpointKey> buckets
        , const EndpointKey&         key
    );

    /**
     * @brief Find bucket index for an occurrence key (if present).
     */
    std::optional<std::size_t> find_bucket(
          std::span<const StopOccurrenceKey> buckets
        , const StopOccurrenceKey&         key
    );

    /**
     * @brief Find bucket index for a physical stop (if present).
     */
    std::optional<std::size_t> find_bucket(
          std::span<const StopId> buckets
        , StopId                key
    );

    /**
     * @brief Lookup physical-space walk ranges by physical origin endpoint.
     */
    SegmentLookup lookup_from(
          const RouteSegmentIndex&        route_index
        , const ConnectionSegmentIndex& connection_index
        , EndpointKey                   physical_from
    );

    /**
     * @brief Lookup physical walk ranges plus exact occurrence-space ranges.
     */
    SegmentLookup lookup_from(
          const RouteSegmentIndex&        route_index
        , const ConnectionSegmentIndex& connection_index
        , StopOccurrenceKey             timed_from
        , EndpointKey                   physical_from
    );

    /**
     * @brief Find the next timed connection segment from an occurrence at or after a time.
     */
    std::optional<ConnectionSegmentId> next_connection_from(
          const ConnectionSegmentIndex& connection_index
        , StopOccurrenceKey           from
        , Time                        time
    );

}  // namespace timetable::domain::preprocessing
