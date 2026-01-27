#pragma once

#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <mathfp/types/strong_type.hpp>
#include <mathfp/types/units.hpp>

namespace timetable::domain {

    // --- Scalar types -----------------------------------------------------------
    using Time   = mathfp::units::Quantity<double, mathfp::units::Time>;
    using Length = mathfp::units::Quantity<double, mathfp::units::Length>;

    // --- Strong domain identifiers ---------------------------------------------
    struct StopIdTag {};
    struct ZoneIdTag {};
    struct LineIdTag {};
    struct RouteIdTag {};
    struct TripIdTag {};
    struct IntervalIdTag {};

    using StopId = mathfp::StrongType<
        std::int64_t
        , StopIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using ZoneId = mathfp::StrongType<
        std::int64_t
        , ZoneIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using LineId = mathfp::StrongType<
        std::int64_t
        , LineIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using RouteId = mathfp::StrongType<
        std::int64_t
        , RouteIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using TripId = mathfp::StrongType<
        std::int64_t
        , TripIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using IntervalId = mathfp::StrongType<
        std::int64_t
        , IntervalIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    // --- Base entities ----------------------------------------------------------
    struct Stop final {
        StopId id{};
        std::optional<ZoneId> zone{};
    };

    struct Zone final {
        ZoneId id{};
    };

    struct Line final {
        LineId id{};
    };

    struct Route final {
        RouteId id{};
        LineId line{};
        std::vector<StopId> stops{};
    };

    struct StopTime final {
        StopId stop{};
        Time arrival{};
        Time departure{};
    };

    struct Trip final {
        TripId id{};
        RouteId route{};
        std::vector<StopTime> times{};
    };

    struct TimeInterval final {
        IntervalId id{};
        Time start{};
        Time end{};
    };

    using WalkEndpoint = std::variant<StopId, ZoneId>;

    struct WalkLink final {
        WalkEndpoint from{};
        WalkEndpoint to{};
        Time walk_time{};
        Length length{};
    };

    struct DemandEntry final {
        ZoneId origin{};
        ZoneId destination{};
        IntervalId interval{};
        double passengers{};
    };

    // --- Input model ------------------------------------------------------------
    struct InputModel final {
        std::vector<Stop> stops{};
        std::vector<Zone> zones{};
        std::vector<Line> lines{};
        std::vector<Route> routes{};
        std::vector<Trip> trips{};
        std::vector<WalkLink> walk_links{};
        std::vector<TimeInterval> intervals{};
        std::vector<DemandEntry> demand{};
    };

}  // namespace timetable::domain
