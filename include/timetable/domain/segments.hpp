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

    enum class CarrierKind : std::uint8_t {
        Line
        , Walk
    };

    using WalkPath       = std::vector<WalkLinkId>;
    using SegmentCarrier = std::variant<LineId, WalkPath>;

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
        std::optional<TripId> trip{};
        std::optional<std::int64_t> from_index{};
        std::optional<std::int64_t> to_index{};
        std::optional<Time> departure{};
        std::optional<Time> arrival{};
        std::optional<double> fare{};
    };

}  // namespace timetable::domain
