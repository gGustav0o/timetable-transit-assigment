#include "timetable/domain/assignment/validation.hpp"

#include <cmath>
#include <map>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "../detail/validation_common.hpp"

namespace timetable::domain::assignment {
    namespace {

        using IntervalMap = std::map<IntervalId, const TimeInterval*>;

        mathfp::Expected<IntervalMap> build_search_task_interval_map(
            const InputModel& input
        ) {
            IntervalMap intervals;
            for (const auto& interval : input.intervals) {
                if (!intervals.emplace(interval.id, &interval).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate time interval id in search task builder input")
                            .ctx("interval_id", interval.id.get())
                    );
                }
                if (!std::isfinite(interval.start.value())
                    || !std::isfinite(interval.end.value())
                    || !(interval.start.value() < interval.end.value())) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("search task builder requires finite intervals satisfying start < end")
                            .ctx("interval_id", interval.id.get())
                            .ctx("start"      , interval.start.value())
                            .ctx("end"        , interval.end.value())
                    );
                }
            }
            return intervals;
        }

        mathfp::Expected<mathfp::Unit> validate_active_assignment_window(
              const DemandEntry&             demand
            , const TimeInterval&            interval
            , const AssignmentPeriodConfig&  assignment_period
        ) {
            const auto window = expand_interval_to_search_window(
                  interval
                , assignment_time_padding(assignment_period)
            );
            if (!std::isfinite(window.begin.value())
                || !std::isfinite(window.end.value())) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("assignment period expansion produced a non-finite search task window")
                        .ctx("origin"     , demand.origin     .get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", interval.id       .get())
                        .ctx("begin"      , window.begin      .value())
                        .ctx("end"        , window.end        .value())
                );
            }
            MATHFP_TRY(validate_search_time_window(window));
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_task_builder_input(
          const InputModel&             input
        , const AssignmentPeriodConfig& assignment_period
    ) {
        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_search_time_padding(
            assignment_time_padding(assignment_period)
        ));

        if (input.demand.empty()) {
            detail::validation::warn(
                "search task builder input: demand is empty; no search tasks will be materialized"
            );
            return mathfp::kUnit;
        }

        MATHFP_TRY_LET(IntervalMap, intervals, build_search_task_interval_map(input));

        bool has_positive_demand = false;
        for (const auto& demand : input.demand) {
            if (!(demand.passengers > 0.0)) {
                continue;
            }
            has_positive_demand = true;

            const auto interval_it = intervals.find(demand.interval);
            if (interval_it == intervals.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("positive demand references unknown interval in search task builder input")
                        .ctx("origin"     , demand.origin     .get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval   .get())
                );
            }

            MATHFP_TRY(validate_active_assignment_window(
                  demand
                , *interval_it->second
                , assignment_period
            ));
        }

        if (!has_positive_demand) {
            detail::validation::warn(
                "search task builder input: no positive-demand entries are present; no search tasks will be materialized"
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
