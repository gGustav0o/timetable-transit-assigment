#pragma once

#include <span>
#include <vector>

#include "timetable/domain/assignment/day_path/signature.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] const DayPathTimedSupport& day_path_support_of(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] const SearchConnection& day_path_representative_connection(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] std::span<const DayPathSupportDescriptor> day_path_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] std::span<const DayPathSupportDescriptor> day_path_split_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept;

    /**
     * @brief Project one validated ride leg to the timed support carrier.
     *
     * The caller must provide a ride leg with supply, line/route/trip and stop
     * occurrence identity already validated by the canonical connection layer.
     */
    [[nodiscard]] DayPathRideSupportLeg day_path_ride_support_leg_of(
        const ConnectionLeg& leg
    );

    [[nodiscard]] std::vector<DayPathRideSupportLeg> day_path_ride_support_legs_of(
        const SearchConnection& connection
    );

    [[nodiscard]] DayPathSupportDescriptor make_day_path_support_descriptor(
          const SearchConnection&     connection
        , const DayPathSignature&     signature
        , CompleteConnectionMetrics   complete_metrics
        , ConnectionMetrics           connection_metrics
    );

    [[nodiscard]] std::vector<CompleteConnectionMetrics> day_path_support_metric_set(
        const DayPathAlternative& alternative
    );

    [[nodiscard]] bool day_path_support_set_dominates(
          const DayPathAlternative& lhs
        , const DayPathAlternative& rhs
    );

}  // namespace timetable::domain::assignment
