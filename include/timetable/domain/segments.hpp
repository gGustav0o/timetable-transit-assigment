#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <variant>
#include <vector>

#include <boost/container_hash/hash.hpp>

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

    enum class CarrierKind : std::uint8_t {
        Line
        , Walk
    };

    enum class EndpointKind : std::uint8_t {
        Stop
        , Zone
    };

    struct EndpointKey final {
        EndpointKind kind{};
        std::int64_t id{};

        auto operator<=>(const EndpointKey&) const = default;
    };

    using WalkPath       = std::vector<WalkLinkId>;
    using SegmentCarrier = std::variant<LineId, WalkPath>;

    [[nodiscard]] constexpr EndpointKey to_endpoint_key(const WalkEndpoint& endpoint) noexcept {
        if (std::holds_alternative<StopId>(endpoint))
            return EndpointKey{ EndpointKind::Stop, std::get<StopId>(endpoint).get() };
        return EndpointKey{ EndpointKind::Zone, std::get<ZoneId>(endpoint).get() };
    }

    [[nodiscard]] constexpr EndpointKey endpoint_key(StopId id) noexcept {
        return EndpointKey{ EndpointKind::Stop, id.get() };
    }

    [[nodiscard]] constexpr EndpointKey endpoint_key(ZoneId id) noexcept {
        return EndpointKey{ EndpointKind::Zone, id.get() };
    }

    [[nodiscard]] constexpr CarrierKind carrier_kind(const SegmentCarrier& carrier) noexcept {
        return std::holds_alternative<LineId>(carrier) ? CarrierKind::Line : CarrierKind::Walk;
    }

    [[nodiscard]] constexpr bool is_line(const SegmentCarrier& carrier) noexcept {
        return carrier_kind(carrier) == CarrierKind::Line;
    }

    [[nodiscard]] constexpr bool is_walk(const SegmentCarrier& carrier) noexcept {
        return carrier_kind(carrier) == CarrierKind::Walk;
    }

    /**
     * @brief Infrastructure-level segment between two nodes.
     *
     * Describes either a transit line segment or a walk path.
     */
    struct RouteSegment final {
        RouteSegmentId id{};
        WalkEndpoint   from{};
        WalkEndpoint   to{};
        Length         length{};
        Time           run_time{};
        SegmentCarrier carrier{};
    };

    /**
     * @brief Timetable-level segment with concrete times (or always-available walk).
     */
    struct ConnectionSegment final {
        ConnectionSegmentId id{};
        RouteSegmentId      route_segment{};
        // Present for transit segments and absent for walk segments.
        std::optional<TripId> trip{};
        // Positions of from/to stops inside the referenced trip.
        std::optional<std::int64_t> from_index{};
        std::optional<std::int64_t> to_index{};
        std::optional<Time> departure{};
        std::optional<Time> arrival{};
        std::optional<double> fare{};
    };

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

    /**
     * @brief Domain rule: a transfer is forbidden if both segments belong to the same trip.
     */
    [[nodiscard]] inline bool forbids_transfer_to_same_trip(
        const ConnectionSegment& current
        , const ConnectionSegment& successor
    ) noexcept {
        return same_trip(current, successor);
    }

}  // namespace timetable::domain

namespace std {
    template <>
    struct hash<timetable::domain::EndpointKey> {
        size_t operator()(const timetable::domain::EndpointKey& k) const noexcept {
            std::size_t seed = 0;
            boost::hash_combine(seed, static_cast<std::uint8_t>(k.kind));
            boost::hash_combine(seed, k.id);
            return seed;
        }
    };
}
