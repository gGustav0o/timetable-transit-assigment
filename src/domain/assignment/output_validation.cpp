#include "detail/output_internal.hpp"

#include <cstddef>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment::detail {

    namespace {

        bool almost_equal_scalar(
              double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_od_result_semantics(
        const AssignmentOdResult& od_result
    ) {
        if (od_result.chosen_connection_count != od_result.connections.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output chosen_connection_count disagrees with connections size")
                    .ctx("origin"        , od_result.origin.get())
                    .ctx("destination"   , od_result.destination.get())
                    .ctx("declared_count", static_cast<std::int64_t>(od_result.chosen_connection_count))
                    .ctx("actual_count"  , static_cast<std::int64_t>(od_result.connections.size()))
            );
        }

        double demand_sum   = 0.0;
        double assigned_sum = 0.0;
        for (const auto& interval : od_result.intervals) {
            double interval_assigned_sum = 0.0;
            for (const auto& share : interval.shares) {
                const auto connection_index = share.connection_index.get();
                if (connection_index < 0
                    || static_cast<std::size_t>(connection_index) >= od_result.connections.size()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output share references connection outside the OD-local connection set")
                            .ctx("origin"          , od_result.origin.get())
                            .ctx("destination"     , od_result.destination.get())
                            .ctx("interval_id"     , interval.interval.id.get())
                            .ctx("connection_index", connection_index)
                            .ctx("connection_count", static_cast<std::int64_t>(od_result.connections.size()))
                    );
                }
                interval_assigned_sum += share.passengers;
            }

            if (!almost_equal_scalar(interval.assigned_passengers, interval_assigned_sum)) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment output interval assigned_passengers disagrees with its shares")
                        .ctx("origin"           , od_result.origin.get())
                        .ctx("destination"      , od_result.destination.get())
                        .ctx("interval_id"      , interval.interval.id.get())
                        .ctx("declared_assigned", interval.assigned_passengers)
                        .ctx("actual_assigned"  , interval_assigned_sum)
                );
            }

            demand_sum += interval.demand_passengers;
            assigned_sum += interval.assigned_passengers;
        }

        if (!almost_equal_scalar(od_result.total_demand_passengers, demand_sum)) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output OD total_demand_passengers disagrees with interval totals")
                    .ctx("origin"               , od_result.origin.get())
                    .ctx("destination"          , od_result.destination.get())
                    .ctx("declared_total_demand", od_result.total_demand_passengers)
                    .ctx("actual_total_demand"  , demand_sum)
            );
        }

        if (!almost_equal_scalar(od_result.assigned_passengers, assigned_sum)) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output OD assigned_passengers disagrees with interval totals")
                    .ctx("origin"           , od_result.origin.get())
                    .ctx("destination"      , od_result.destination.get())
                    .ctx("declared_assigned", od_result.assigned_passengers)
                    .ctx("actual_assigned"  , assigned_sum)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_output_summary_semantics(
        const AssignmentOutput& output
    ) {
        std::size_t search_count = 0;
        std::size_t chosen_count = 0;
        std::size_t share_count  = 0;

        double total_demand = 0.0;
        double assigned     = 0.0;

        for (const auto& od_result : output.od_results) {
            MATHFP_TRY(validate_od_result_semantics(od_result));
            search_count += od_result.search_connection_count;
            chosen_count += od_result.chosen_connection_count;
            total_demand += od_result.total_demand_passengers;
            assigned += od_result.assigned_passengers;
            for (const auto& interval : od_result.intervals) {
                share_count += interval.shares.size();
            }
        }

        if (output.summary.od_count != output.od_results.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output summary od_count disagrees with OD result count")
                    .ctx("declared_od_count", static_cast<std::int64_t>(output.summary.od_count))
                    .ctx("actual_od_count"  , static_cast<std::int64_t>(output.od_results.size()))
            );
        }

        if (output.summary.search_connection_count != search_count
            || output.summary.chosen_connection_count != chosen_count
            || output.summary.demand_share_count != share_count
            || !almost_equal_scalar(output.summary.total_demand_passengers, total_demand)
            || !almost_equal_scalar(output.summary.assigned_passengers, assigned)) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment output summary disagrees with OD aggregates")
                    .ctx("declared_search_connections", static_cast<std::int64_t>(output.summary.search_connection_count))
                    .ctx("actual_search_connections"  , static_cast<std::int64_t>(search_count))
                    .ctx("declared_chosen_connections", static_cast<std::int64_t>(output.summary.chosen_connection_count))
                    .ctx("actual_chosen_connections"  , static_cast<std::int64_t>(chosen_count))
                    .ctx("declared_share_count"       , static_cast<std::int64_t>(output.summary.demand_share_count))
                    .ctx("actual_share_count"         , static_cast<std::int64_t>(share_count))
                    .ctx("declared_total_demand"      , output.summary.total_demand_passengers)
                    .ctx("actual_total_demand"        , total_demand)
                    .ctx("declared_assigned"          , output.summary.assigned_passengers)
                    .ctx("actual_assigned"            , assigned)
            );
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::detail
