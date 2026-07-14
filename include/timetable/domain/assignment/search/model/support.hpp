#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include <boost/container/small_vector.hpp>

#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    /**
     * Compact timetable support carried by a production OD-day branch.
     *
     * This is not the structural tree label. It is the extension envelope
     * needed to validate future schedule support without making concrete
     * timed segments part of the day-path identity.
     */
    struct TimedSupportLabel final {
        ConnectionSegmentId          connection;
        RouteSegmentId               route_segment;
        std::optional<TripId>        trip{};
        std::optional<RoutePosition> from_index{};
        std::optional<RoutePosition> to_index{};
        Time                         departure{};
        Time                         arrival{};

        bool operator==(const TimedSupportLabel&) const = default;
    };

    inline constexpr std::size_t kTimedSupportEnvelopeInlineLabels = 8u;

    using TimedSupportLabelVector = boost::container::small_vector<
          TimedSupportLabel
        , kTimedSupportEnvelopeInlineLabels
    >;

    struct TimedSupportEnvelopeKey final {
        std::optional<StopOccurrenceKey> last_timed_occurrence{};
        std::optional<LineId>            last_line{};

        bool operator==(const TimedSupportEnvelopeKey&) const = default;
    };

    struct TimedSupportEnvelope final {
        TimedSupportEnvelopeKey key{};
        TimedSupportLabelVector labels{};

        bool operator==(const TimedSupportEnvelope&) const = default;
    };

    struct OdDayPathPrefixNode final {
        std::shared_ptr<const OdDayPathPrefixNode> parent{};
        DayPathLeg                                leg;
        std::size_t                               length{};
    };

    struct OdDayPathPrefix final {
        ZoneId                                     origin;
        std::shared_ptr<const OdDayPathPrefixNode> tail{};
        std::size_t                                length{};
    };

    [[nodiscard]] OdDayPathPrefix make_od_day_path_prefix(
        ZoneId origin
    ) noexcept;

    [[nodiscard]] OdDayPathPrefix append_od_day_path_leg(
          OdDayPathPrefix prefix
        , DayPathLeg       leg
    );

    [[nodiscard]] DayPathPrefix materialize_day_path_prefix(
        const OdDayPathPrefix& prefix
    );

    struct OdDaySupportPrefixNode;

    using OdDaySupportPrefix = std::shared_ptr<const OdDaySupportPrefixNode>;

    struct OdDaySupportPrefixNode final {
        OdDaySupportPrefix  parent{};
        ConnectionSegmentId segment;
        std::size_t         length{};
    };

    [[nodiscard]] OdDaySupportPrefix append_od_day_support_segment(
          OdDaySupportPrefix prefix
        , ConnectionSegmentId segment
    );

    [[nodiscard]] std::vector<ConnectionSegmentId> materialize_od_day_support_segments(
        const OdDaySupportPrefix& prefix
    );

}  // namespace timetable::domain::assignment
