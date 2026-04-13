#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <mathfp/types/strong_type.hpp>

#include "timetable/domain/fare.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

    // --- Strong domain identifiers ---------------------------------------------
    struct StopIdTag {};
    struct ZoneIdTag {};
    struct LineIdTag {};
    struct RouteIdTag {};
    struct TripIdTag {};
    struct IntervalIdTag {};
    struct WalkLinkIdTag {};
    struct RoutePositionTag {};

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

    using WalkLinkId = mathfp::StrongType<
          std::int64_t
        , WalkLinkIdTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    using RoutePosition = mathfp::StrongType<
          std::int64_t
        , RoutePositionTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    // --- Base entities ----------------------------------------------------------
    struct Stop final {
        StopId                id{};
        std::optional<ZoneId> zone{};
    };

    struct Zone final {
        ZoneId id{};
    };

    /**
     * @brief Transit service line metadata.
     *
     * fare models the additive cost of boarding and riding a timed segment on
     * the line in the raw timetable pipeline. Pre-segmented inputs may leave it
     * empty because their connection segments already carry fare directly.
     */
    struct Line final {
        LineId                  id{};
        std::optional<LineFare> fare{};
    };

    struct Route final {
        RouteId             id{};
        LineId              line{};
        std::vector<StopId> stops{};
    };

    struct StopTime final {
        StopId stop{};
        Time   arrival{};
        Time   departure{};
    };

    /**
     * @brief Stop occurrence on a route/trip order axis.
     *
     * A physical stop may appear multiple times on a loop line. RoutePosition
     * distinguishes these occurrences without changing the physical stop model.
     */
    struct StopOccurrence final {
        StopId        stop{};
        RoutePosition position{};

        auto operator<=>(const StopOccurrence&) const = default;
    };

    struct Trip final {
        TripId                id{};
        RouteId               route{};
        std::vector<StopTime> times{};
    };

    struct TimeInterval final {
        IntervalId id{};
        Time       start{};
        Time       end{};
    };

    /**
     * @brief Physical endpoint used by access/egress/walk topology.
     *
     * Timetable line topology may need StopOccurrence instead of bare StopId.
     */
    using WalkEndpoint = std::variant<StopId, ZoneId>;

    struct WalkLink final {
        WalkLinkId   id{};
        WalkEndpoint from{};
        WalkEndpoint to{};
        Time         walk_time{};
        Length       length{};
    };

    struct DemandEntry final {
        ZoneId     origin{};
        ZoneId     destination{};
        IntervalId interval{};
        double     passengers{};
    };

    // --- Input model ------------------------------------------------------------
    struct InputModel final {
        std::vector<Stop>         stops{};
        std::vector<Zone>         zones{};
        std::vector<Line>         lines{};
        std::vector<Route>        routes{};
        std::vector<Trip>         trips{};
        std::vector<WalkLink>     walk_links{};
        std::vector<TimeInterval> intervals{};
        std::vector<DemandEntry>  demand{};
    };

}  // namespace timetable::domain
