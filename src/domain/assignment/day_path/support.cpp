#include "timetable/domain/assignment/day_path/support.hpp"

#include <algorithm>
#include <span>
#include <vector>

namespace timetable::domain::assignment {

    const DayPathTimedSupport& day_path_support_of(
        const DayPathAlternative& alternative
    ) noexcept {
        return alternative.support;
    }

    const SearchConnection& day_path_representative_connection(
        const DayPathAlternative& alternative
    ) noexcept {
        return alternative.support.representative;
    }

    std::span<const DayPathSupportDescriptor> day_path_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept {
        return day_path_split_support_descriptors(alternative);
    }

    std::span<const DayPathSupportDescriptor> day_path_split_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept {
        return std::span<const DayPathSupportDescriptor>{
              alternative.support.split_support.supports.data()
            , alternative.support.split_support.supports.size()
        };
    }

    DayPathRideSupportLeg day_path_ride_support_leg_of(
        const ConnectionLeg& leg
    ) {
        return DayPathRideSupportLeg{
              .connection_segment = *leg.connection_segment
            , .route_segment      = *leg.route_segment
            , .line               = *leg.line
            , .route              = *leg.route
            , .trip               = *leg.trip
            , .occurrence_from    = *leg.occurrence_from
            , .occurrence_to      = *leg.occurrence_to
            , .from_index         = leg.occurrence_from->position
            , .to_index           = leg.occurrence_to->position
            , .departure          = leg.start_time
            , .arrival            = leg.end_time
        };
    }

    std::vector<DayPathRideSupportLeg> day_path_ride_support_legs_of(
        const SearchConnection& connection
    ) {
        std::vector<DayPathRideSupportLeg> ride_legs;
        const auto& trace = canonical_connection(connection).trace;
        ride_legs.reserve(trace.legs.size());
        for (const auto& leg : trace.legs) {
            if (is_ride_leg(leg.kind)) {
                ride_legs.push_back(day_path_ride_support_leg_of(leg));
            }
        }
        return ride_legs;
    }

    DayPathSupportDescriptor make_day_path_support_descriptor(
          const SearchConnection&    connection
        , const DayPathSignature&    signature
        , CompleteConnectionMetrics  complete_metrics
        , ConnectionMetrics          connection_metrics
    ) {
        return DayPathSupportDescriptor{
              .signature          = signature
            , .complete_metrics   = complete_metrics
            , .connection_metrics = connection_metrics
            , .ride_legs          = day_path_ride_support_legs_of(connection)
        };
    }

    std::vector<CompleteConnectionMetrics> day_path_support_metric_set(
        const DayPathAlternative& alternative
    ) {
        std::vector<CompleteConnectionMetrics> metrics;
        metrics.reserve(
            alternative.support.split_support.supports.empty()
                ? 1u
                : alternative.support.split_support.supports.size()
        );
        for (const auto& support : alternative.support.split_support.supports) {
            metrics.push_back(support.complete_metrics);
        }
        if (metrics.empty()) {
            metrics.push_back(alternative.support.representative_metrics);
        }
        return metrics;
    }

    bool day_path_support_set_dominates(
          const DayPathAlternative& lhs
        , const DayPathAlternative& rhs
    ) {
        const auto lhs_metrics = day_path_support_metric_set(lhs);
        const auto rhs_metrics = day_path_support_metric_set(rhs);
        return std::all_of(
              rhs_metrics.begin()
            , rhs_metrics.end()
            , [&](const CompleteConnectionMetrics& rhs_support) {
                  return std::any_of(
                        lhs_metrics.begin()
                      , lhs_metrics.end()
                      , [&](const CompleteConnectionMetrics& lhs_support) {
                            return complete_connection_dominates(
                                  lhs_support
                                , rhs_support
                            );
                        }
                  );
              }
        );
    }

}  // namespace timetable::domain::assignment
