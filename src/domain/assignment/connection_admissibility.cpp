#include "timetable/domain/assignment/connection_admissibility.hpp"

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool reference_time_outside_window(
              const ConnectionMetrics& metrics
            , const AssignmentTimeWindow& window
            , DemandSegmentBasis basis
        ) noexcept {
            return !contains(window, demand_segment_reference_time(metrics, basis));
        }

        [[nodiscard]] bool deletes_positive_delta_t(
              const ConnectionMetrics& metrics
            , const TimeInterval&      interval
            , const DemandSegmentTimeConfig& config
        ) noexcept {
            if (config.consider_connections_with_positive_delta_t) {
                return false;
            }
            return demand_segment_delta_t(metrics, interval, config.basis) > 0.0;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_demand_segment_time_config(
        const DemandSegmentTimeConfig& config
    ) {
        switch (config.basis) {
            case DemandSegmentBasis::Departure:
            case DemandSegmentBasis::Arrival:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unsupported demand segment basis")
                .ctx("basis", static_cast<std::int64_t>(config.basis))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_connection_deletion_config(
        const ConnectionDeletionConfig&
    ) {
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_connection_admissibility_config(
        const ConnectionAdmissibilityConfig& config
    ) {
        MATHFP_TRY(validate_connection_deletion_config(config.deletion));
        MATHFP_TRY(validate_demand_segment_time_config(config.demand_time));
        return mathfp::kUnit;
    }

    AssignmentTimeWindow assignment_period_window(
          const TimeInterval&           interval
        , const AssignmentPeriodConfig& assignment_period
    ) noexcept {
        return AssignmentTimeWindow{
              .begin = Time{
                  interval.start.value() - assignment_period.pre_assign_period.value()
              }
            , .end = Time{
                  interval.end.value() + assignment_period.post_assign_period.value()
              }
        };
    }

    bool contains(
          const AssignmentTimeWindow& window
        , Time                        value
    ) noexcept {
        return window.begin.value() <= value.value()
            && value.value() <= window.end.value();
    }

    Time demand_segment_reference_time(
          const ConnectionMetrics& metrics
        , DemandSegmentBasis       basis
    ) noexcept {
        switch (basis) {
            case DemandSegmentBasis::Departure:
                return metrics.departure_time;
            case DemandSegmentBasis::Arrival:
                return metrics.arrival_time;
        }
        return metrics.departure_time;
    }

    double demand_segment_delta_t(
          const ConnectionMetrics& metrics
        , const TimeInterval&      interval
        , DemandSegmentBasis       basis
    ) noexcept {
        switch (basis) {
            case DemandSegmentBasis::Departure:
                return metrics.departure_time.value() - interval.end.value();
            case DemandSegmentBasis::Arrival:
                return interval.start.value() - metrics.arrival_time.value();
        }
        return metrics.departure_time.value() - interval.end.value();
    }

    ConnectionAssignmentPeriodRelation classify_assignment_period_relation(
          const ConnectionMetrics& metrics
        , const AssignmentTimeWindow& window
    ) noexcept {
        return ConnectionAssignmentPeriodRelation{
              .departure_before = metrics.departure_time.value() < window.begin.value()
            , .departure_after  = metrics.departure_time.value() > window.end.value()
            , .arrival_before   = metrics.arrival_time.value() < window.begin.value()
            , .arrival_after    = metrics.arrival_time.value() > window.end.value()
        };
    }

    bool connection_deleted_by_assignment_period(
          const ConnectionMetrics&             metrics
        , const TimeInterval&                  interval
        , const AssignmentPeriodConfig&        assignment_period
        , const ConnectionAdmissibilityConfig& config
    ) noexcept {
        const auto window = assignment_period_window(interval, assignment_period);
        const auto relation = classify_assignment_period_relation(metrics, window);

        if (config.deletion.delete_outside_assignment_period
            && reference_time_outside_window(metrics, window, config.demand_time.basis)) {
            return true;
        }

        if (config.demand_time.basis == DemandSegmentBasis::Departure
            && config.deletion.delete_departures_before_assignment_period_for_departure_based
            && relation.departure_before) {
            return true;
        }

        if (config.demand_time.basis == DemandSegmentBasis::Arrival
            && config.deletion.delete_arrivals_after_assignment_period_for_arrival_based
            && relation.arrival_after) {
            return true;
        }

        return deletes_positive_delta_t(metrics, interval, config.demand_time);
    }

    bool connection_admissible_for_assignment_period(
          const ConnectionMetrics&             metrics
        , const TimeInterval&                  interval
        , const AssignmentPeriodConfig&        assignment_period
        , const ConnectionAdmissibilityConfig& config
    ) noexcept {
        return !connection_deleted_by_assignment_period(
              metrics
            , interval
            , assignment_period
            , config
        );
    }

}  // namespace timetable::domain::assignment
