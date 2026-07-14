#pragma once

#include <compare>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/complete_connection_metrics.hpp"
#include "timetable/domain/assignment/search/connection.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    struct DayPathLeg final {
        ConnectionLegKind                kind{};
        std::optional<RouteSegmentId>    route_segment{};
        EndpointKey                      physical_from{};
        EndpointKey                      physical_to{};
        std::optional<StopOccurrenceKey> occurrence_from{};
        std::optional<StopOccurrenceKey> occurrence_to{};
        std::optional<LineId>            line{};
        std::optional<RouteId>           route{};

        auto operator<=>(const DayPathLeg&) const = default;
    };

    struct DayPathSignature final {
        ZoneId                  origin;
        ZoneId                  destination;
        std::vector<DayPathLeg> legs{};

        auto operator<=>(const DayPathSignature&) const = default;
    };

    struct DayPathIdentity final {
        DayPathSignature signature{};
    };

    struct DayPathRideSupportLeg final {
        ConnectionSegmentId connection_segment;
        RouteSegmentId      route_segment;
        LineId              line;
        RouteId             route;
        TripId              trip;
        StopOccurrenceKey   occurrence_from;
        StopOccurrenceKey   occurrence_to;
        RoutePosition       from_index;
        RoutePosition       to_index;
        Time                departure{};
        Time                arrival{};

        auto operator<=>(const DayPathRideSupportLeg&) const = default;
    };

    struct DayPathSupportDescriptor final {
        DayPathSignature                   signature{};
        CompleteConnectionMetrics          complete_metrics{};
        ConnectionMetrics                  connection_metrics{};
        std::vector<DayPathRideSupportLeg> ride_legs{};
    };

    struct DayPathSplitSupport final {
        std::vector<DayPathSupportDescriptor> supports{};
    };

    struct DayPathTimedSupport final {
        SearchConnection          representative;
        CompleteConnectionMetrics representative_metrics{};
        ConnectionMetrics         representative_connection_metrics{};
        DayPathSplitSupport       split_support{};
    };

    struct DayPathAlternative final {
        DayPathIdentity     identity{};
        DayPathTimedSupport support;
    };

    struct OdDayPathPairResult final {
        ZoneId                          origin;
        ZoneId                          destination;
        std::vector<DayPathAlternative> alternatives{};
    };

    struct OdDayOriginPathSet final {
        ZoneId                           origin;
        std::vector<OdDayPathPairResult> pair_results{};
    };

    struct OdDayPathSearchResult final {
        std::vector<OdDayOriginPathSet> origin_results{};
    };

    using OdDayPairResult = OdDayPathPairResult;
    using OriginDaySearchResult = OdDayOriginPathSet;
    using OdDayConnectionSearchResult = OdDayPathSearchResult;

    struct OdDayPairConnectionCount final {
        ZoneId      origin;
        ZoneId      destination;
        std::size_t connection_count{};
    };

    struct OdDayPathSearchSummary final {
        std::vector<OdDayPairConnectionCount> pair_counts{};
    };

    using OdDayConnectionSearchSummary = OdDayPathSearchSummary;

    using OdDayOriginPathSetSink =
        std::function<mathfp::Expected<mathfp::Unit>(OdDayOriginPathSet)>;

    using OdDayOriginResultSink = OdDayOriginPathSetSink;

    [[nodiscard]] std::size_t search_connection_count(
        const OdDayPathSearchResult& result
    ) noexcept;

    [[nodiscard]] std::size_t search_connection_count(
        const OdDayPathSearchSummary& summary
    ) noexcept;

}  // namespace timetable::domain::assignment
