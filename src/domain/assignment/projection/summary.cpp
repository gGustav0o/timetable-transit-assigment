#include "timetable/domain/assignment/projection/summary.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>

namespace timetable::domain::assignment::projection {
    namespace {

        struct ConnectionShareAggregate final {
            mathfp::CompensatedSum<double> assigned_passengers{};
            std::size_t share_count{};
        };

        mathfp::Expected<std::vector<ConnectionShareAggregate>> aggregate_connection_shares(
            const AssignmentOdResult& od_result
        ) {
            std::vector<ConnectionShareAggregate> aggregates(od_result.connections.size());

            for (const auto& interval : od_result.intervals) {
                for (const auto& share : interval.shares) {
                    const auto raw_index = share.connection_index.get();
                    if (raw_index < 0) {
                        return mathfp::unexpected(
                            mathfp::internal_error("assignment summary projection encountered negative connection reference")
                                .ctx("origin"          , od_result.origin     .get())
                                .ctx("destination"     , od_result.destination.get())
                                .ctx("interval_id"     , interval.interval.id .get())
                                .ctx("connection_index", raw_index)
                        );
                    }

                    const auto index = static_cast<std::size_t>(raw_index);
                    if (index >= aggregates.size()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("assignment summary projection encountered out-of-range connection reference")
                                .ctx("origin"          , od_result.origin.get())
                                .ctx("destination"     , od_result.destination.get())
                                .ctx("interval_id"     , interval.interval.id.get())
                                .ctx("connection_index", raw_index)
                                .ctx("connection_count", static_cast<std::int64_t>(aggregates.size()))
                        );
                    }

                    aggregates[index].assigned_passengers.add(share.passengers);
                    aggregates[index].share_count += 1;
                }
            }

            return aggregates;
        }

        void update_best_time(
              std::optional<Time>& current
            , Time                 candidate
        ) {
            if (!current.has_value() || candidate.value() < current->value()) {
                current = candidate;
            }
        }

        void update_best_scalar(
              std::optional<double>& current
            , double                 candidate
        ) {
            if (!current.has_value() || candidate < *current) {
                current = candidate;
            }
        }

        void update_best_transfers(
              std::optional<TransferCount>& current
            , TransferCount                 candidate
        ) {
            if (!current.has_value() || candidate.get() < current->get()) {
                current = candidate;
            }
        }

        mathfp::Expected<AssignmentOdSummary> build_od_summary(
            const AssignmentOdResult& od_result
        ) {
            auto share_aggregates_result = aggregate_connection_shares(od_result);
            if (!share_aggregates_result) {
                return mathfp::unexpected(std::move(share_aggregates_result.error()));
            }
            const auto& share_aggregates = *share_aggregates_result;

            AssignmentOdSummary summary{
                  .origin                   = od_result.origin
                , .destination              = od_result.destination
                , .search_connection_count  = od_result.search_connection_count
                , .chosen_connection_count  = od_result.chosen_connection_count
                , .interval_count           = od_result.intervals.size()
                , .share_count              = 0
                , .total_demand_passengers  = od_result.total_demand_passengers
                , .assigned_passengers      = od_result.assigned_passengers
                , .fastest_journey_time     = std::nullopt
                , .lowest_fare              = std::nullopt
                , .minimum_transfers        = std::nullopt
                , .connections              = {}
            };

            summary.connections.reserve(od_result.connections.size());
            for (std::size_t i = 0; i < od_result.connections.size(); ++i) {
                const auto& connection = od_result.connections[i].summary;
                const auto metrics     = metrics_of(connection);
                const auto transfer_time =
                    metrics.transfer_wait_time + metrics.transfer_walk_time;
                const auto& shares = share_aggregates[i];

                update_best_time     (summary.fastest_journey_time    , metrics.journey_time);
                update_best_scalar   (summary.lowest_fare             , metrics.fare);
                update_best_transfers(summary.minimum_transfers       , metrics.transfer_count);
                summary.share_count += shares.share_count;

                summary.connections.push_back(
                    AssignmentConnectionSummary{
                          .index               = AssignmentConnectionRef{ static_cast<std::int64_t>(i) }
                        , .departure           = metrics.departure_time
                        , .arrival             = metrics.arrival_time
                        , .journey_time        = metrics.journey_time
                        , .in_vehicle_time     = metrics.in_vehicle_time
                        , .access_time         = metrics.access_time
                        , .egress_time         = metrics.egress_time
                        , .transfer_walk_time  = metrics.transfer_walk_time
                        , .transfer_wait_time  = metrics.transfer_wait_time
                        , .transfer_time       = transfer_time
                        , .transfers           = metrics.transfer_count
                        , .fare                = metrics.fare
                        , .assigned_passengers = shares    .assigned_passengers.value()
                        , .share_count         = shares    .share_count
                    }
                );
            }

            return summary;
        }

    }  // namespace

    mathfp::Expected<AssignmentResultSummary> build_assignment_result_summary(
        const AssignmentOutput& output
    ) {
        AssignmentResultSummary summary{
              .totals              = output.summary
            , .time_interval_count = 0
            , .task_count          = 0
            , .nonempty_od_count   = 0
            , .line_load_count     = output.loads.line_loads.size()
            , .route_load_count    = output.loads.route_loads.size()
            , .trip_load_count     = output.loads.trip_loads.size()
            , .segment_load_count  = output.loads.segment_loads.size()
            , .od_results          = {}
        };
        summary.od_results.reserve(output.od_results.size());

        std::set<IntervalId> time_intervals;
        for (const auto& od_result : output.od_results) {
            auto od_summary_result = build_od_summary(od_result);
            if (!od_summary_result) {
                return mathfp::unexpected(std::move(od_summary_result.error()));
            }

            summary.task_count += od_summary_result->interval_count;
            for (const auto& interval : od_result.intervals) {
                time_intervals.insert(interval.interval.id);
            }
            if (!od_summary_result->connections.empty() || !od_result.intervals.empty()) {
                summary.nonempty_od_count += 1;
            }
            summary.od_results.push_back(std::move(*od_summary_result));
        }
        summary.time_interval_count = time_intervals.size();

        return summary;
    }

}  // namespace timetable::domain::assignment::projection
