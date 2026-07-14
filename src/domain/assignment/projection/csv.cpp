#include "timetable/domain/assignment/projection/csv.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment::projection {
    namespace {

        mathfp::Expected<mathfp::Unit> ensure_matching_od_summary(
              const AssignmentOdResult&  od_result
            , const AssignmentOdSummary& summary
        ) {
            if (
                   od_result.origin      != summary.origin
                || od_result.destination != summary.destination
            ) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment csv projection encountered mismatched OD summary ordering")
                        .ctx("od_origin"          , od_result.origin     .get())
                        .ctx("od_destination"     , od_result.destination.get())
                        .ctx("summary_origin"     , summary  .origin     .get())
                        .ctx("summary_destination", summary  .destination.get())
                );
            }

            if (od_result.connections.size() != summary.connections.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment csv projection encountered mismatched connection summary count")
                        .ctx("origin"                  , od_result.origin     .get())
                        .ctx("destination"             , od_result.destination.get())
                        .ctx("connection_count"        , static_cast<std::int64_t>(od_result.connections.size()))
                        .ctx("summary_connection_count", static_cast<std::int64_t>(summary  .connections.size()))
                );
            }

            return mathfp::kUnit;
        }

        AssignmentOdSummaryCsvRow build_od_summary_row(
            const AssignmentOdSummary& summary
        ) {
            return AssignmentOdSummaryCsvRow{
                  .origin                   = summary.origin
                , .destination              = summary.destination
                , .search_connection_count  = summary.search_connection_count
                , .chosen_connection_count  = summary.chosen_connection_count
                , .structural_day_path_count =
                      summary.structural_day_path_count
                , .timed_support_alternative_count =
                      summary.timed_support_alternative_count
                , .interval_admissible_split_alternative_count =
                      summary.interval_admissible_split_alternative_count
                , .unassigned_demand_count =
                      summary.unassigned_demand_count
                , .interval_count           = summary.interval_count
                , .share_count              = summary.share_count
                , .total_demand_passengers  = summary.total_demand_passengers
                , .assigned_passengers      = summary.assigned_passengers
                , .unassigned_passengers    = summary.unassigned_passengers
                , .fastest_journey_time     = summary.fastest_journey_time
                , .lowest_fare              = summary.lowest_fare
                , .minimum_transfers        = summary.minimum_transfers
            };
        }

        mathfp::Expected<AssignmentConnectionCsvRow> build_connection_row(
              const AssignmentOdResult&          od_result
            , const AssignmentConnectionSummary& summary
            , const AssignmentConnection&        connection
            , AssignmentConnectionRef            expected_index
        ) {
            const auto raw_index = summary.index.get();
            if (raw_index < 0) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment csv projection encountered negative connection index")
                        .ctx("origin"          , od_result.origin     .get())
                        .ctx("destination"     , od_result.destination.get())
                        .ctx("connection_index", raw_index)
                );
            }

            if (summary.index != expected_index) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment csv projection encountered mismatched connection index ordering")
                        .ctx("origin"                   , od_result.origin     .get())
                        .ctx("destination"              , od_result.destination.get())
                        .ctx("expected_connection_index", expected_index       .get())
                        .ctx("summary_connection_index" , summary.index        .get())
                );
            }

            return AssignmentConnectionCsvRow{
                  .origin              = od_result .origin
                , .destination         = od_result .destination
                , .connection_index    = summary   .index
                , .departure           = summary   .departure
                , .arrival             = summary   .arrival
                , .journey_time        = summary   .journey_time
                , .in_vehicle_time     = summary   .in_vehicle_time
                , .access_time         = summary   .access_time
                , .egress_time         = summary   .egress_time
                , .transfer_walk_time  = summary   .transfer_walk_time
                , .transfer_wait_time  = summary   .transfer_wait_time
                , .transfer_time       = summary   .transfer_time
                , .transfers           = summary   .transfers
                , .fare                = summary   .fare
                , .assigned_passengers = summary   .assigned_passengers
                , .share_count         = summary   .share_count
                , .path_segment_count  = connection.segments.size()
            };
        }

        std::vector<AssignmentShareCsvRow> build_share_rows(
            const AssignmentOdResult& od_result
        ) {
            std::vector<AssignmentShareCsvRow> rows{};

            for (const auto& interval : od_result.intervals) {
                for (const auto& share : interval.shares) {
                    rows.push_back(
                        AssignmentShareCsvRow{
                              .origin                       = od_result.origin
                            , .destination                  = od_result.destination
                            , .interval_id                  = interval .interval.id
                            , .interval_start               = interval .interval.start
                            , .interval_end                 = interval .interval.end
                            , .interval_demand_passengers   = interval .demand_passengers
                            , .interval_assigned_passengers = interval .assigned_passengers
                            , .connection_index             = share    .connection_index
                            , .share_passengers             = share    .passengers
                            , .probability                  = share    .probability
                            , .independence                 = share    .independence
                            , .split_impedance              = share    .split_impedance
                        }
                    );
                }
            }

            return rows;
        }

        std::size_t count_od_segment_rows(
            const AssignmentOdResult& od_result
        ) {
            std::size_t count = 0;
            for (const auto& connection : od_result.connections) {
                count += connection.segments.size();
            }
            return count;
        }

        std::size_t count_total_segment_rows(
            const AssignmentOutput& output
        ) {
            std::size_t count = 0;
            for (const auto& od_result : output.od_results) {
                count += count_od_segment_rows(od_result);
            }
            return count;
        }

        std::size_t count_total_load_rows(
            const AssignmentOutput& output
        ) noexcept {
            return output.loads.line_loads.size()
                 + output.loads.route_loads.size()
                 + output.loads.trip_loads.size()
                 + output.loads.segment_loads.size();
        }

        std::size_t count_total_vehicle_journey_item_load_rows(
            const AssignmentOutput& output
        ) noexcept {
            return output.vehicle_journey_item_loads.items.size();
        }

        AssignmentMetadataCsvRow build_metadata_row(
            const AssignmentOutput& output
        ) {
            return AssignmentMetadataCsvRow{
                  .mode                    = output.mode
                , .skim_status             = output.skim_matrix.status
                , .od_count                = output.summary.od_count
                , .search_connection_count = searched_alternative_count(output.summary)
                , .chosen_connection_count = chosen_alternative_count(output.summary)
                , .demand_share_count      = output.summary.demand_share_count
                , .structural_day_path_count =
                      output.summary.structural_day_path_count
                , .timed_support_alternative_count =
                      output.summary.timed_support_alternative_count
                , .interval_admissible_split_alternative_count =
                      output.summary.interval_admissible_split_alternative_count
                , .unassigned_demand_count =
                      output.summary.unassigned_demand_count
                , .skim_entry_count        = output.skim_matrix.entries.size()
                , .total_demand_passengers = output.summary.total_demand_passengers
                , .assigned_passengers     = output.summary.assigned_passengers
                , .unassigned_passengers   = output.summary.unassigned_passengers
                , .runtime_seconds         = output.summary.runtime_seconds
            };
        }

        AssignmentSegmentCsvRow build_segment_row(
              const AssignmentOdResult&    od_result
            , AssignmentConnectionRef      connection_index
            , std::size_t                  path_segment_index
            , const AssignmentPathSegment& segment
        ) {
            const auto physical_from = physical_from_key(segment.route_segment);
            const auto physical_to   = physical_to_key  (segment.route_segment);

            AssignmentSegmentCsvRow row{
                  .origin                = od_result.origin
                , .destination           = od_result.destination
                , .connection_index      = connection_index
                , .path_segment_index    = path_segment_index
                , .connection_segment_id = segment.connection_segment.id
                , .route_segment_id      = segment.route_segment.id
                , .route_topology_kind   = route_topology_kind(segment.route_segment)
                , .physical_from_kind    = physical_from.kind
                , .physical_from_id      = physical_from.id
                , .physical_to_kind      = physical_to.kind
                , .physical_to_id        = physical_to.id
                , .route_length          = segment.route_segment.length
                , .route_run_time        = segment.route_segment.run_time
                , .walk_path_link_count  = 0
                , .line_id               = std::nullopt
                , .route_id              = std::nullopt
                , .line_from_stop_id     = std::nullopt
                , .line_from_position    = std::nullopt
                , .line_to_stop_id       = std::nullopt
                , .line_to_position      = std::nullopt
                , .trip_id               = segment.connection_segment.trip
                , .connection_from_index = segment.connection_segment.from_index
                , .connection_to_index   = segment.connection_segment.to_index
                , .departure             = segment.connection_segment.departure
                , .arrival               = segment.connection_segment.arrival
                , .fare                  = segment.connection_segment.fare
            };

            if (const auto* walk = walk_topology_of(segment.route_segment)) {
                row.walk_path_link_count = walk->path.size();
                return row;
            }

            const auto* line       = line_topology_of(segment.route_segment);
            row.line_id            = line->line;
            row.route_id           = line->route;
            row.line_from_stop_id  = line->from.stop;
            row.line_from_position = line->from.position;
            row.line_to_stop_id    = line->to.stop;
            row.line_to_position   = line->to.position;
            return row;
        }

        std::vector<AssignmentSegmentCsvRow> build_segment_rows(
              const AssignmentOdResult&   od_result
            , AssignmentConnectionRef     connection_index
            , const AssignmentConnection& connection
        ) {
            std::vector<AssignmentSegmentCsvRow> rows{};
            rows.reserve(connection.segments.size());

            for (std::size_t i = 0; i < connection.segments.size(); ++i) {
                rows.push_back(
                    build_segment_row(
                          od_result
                        , connection_index
                        , i
                        , connection.segments[i]
                    )
                );
            }

            return rows;
        }

        mathfp::Expected<mathfp::Unit> append_connection_projection_rows(
              AssignmentCsvProjection&   projection
            , const AssignmentOdResult&  od_result
            , const AssignmentOdSummary& od_summary
        ) {
            for (std::size_t connection_index = 0; connection_index < od_result.connections.size(); ++connection_index) {
                const auto connection_ref = AssignmentConnectionRef{
                    static_cast<std::int64_t>(connection_index)
                };

                MATHFP_TRY_LET(
                      AssignmentConnectionCsvRow
                    , connection_row
                    , build_connection_row(
                          od_result
                        , od_summary.connections[connection_index]
                        , od_result .connections[connection_index]
                        , connection_ref
                    )
                );
                projection.connection_rows.push_back(std::move(connection_row));

                auto segment_rows = build_segment_rows(
                      od_result
                    , connection_ref
                    , od_result.connections[connection_index]
                );
                projection.segment_rows.insert(
                      projection.segment_rows.end()
                    , segment_rows.begin()
                    , segment_rows.end()
                );
            }

            return mathfp::kUnit;
        }

        void append_share_projection_rows(
              AssignmentCsvProjection&  projection
            , const AssignmentOdResult& od_result
        ) {
            auto share_rows = build_share_rows(od_result);
            projection.share_rows.insert(
                  projection.share_rows.end()
                , share_rows.begin()
                , share_rows.end()
            );
        }

        void append_load_projection_rows(
              AssignmentCsvProjection& projection
            , const AssignmentLoads&   loads
        ) {
            for (const auto& load : loads.line_loads) {
                projection.load_rows.push_back(
                    AssignmentLoadCsvRow{
                          .level                  = AssignmentLoadLevel::Line
                        , .interval_id            = load.interval
                        , .line_id                = load.line
                        , .route_id               = std::nullopt
                        , .trip_id                = std::nullopt
                        , .route_segment_id       = std::nullopt
                        , .connection_segment_id  = std::nullopt
                        , .from_stop_id           = std::nullopt
                        , .from_position          = std::nullopt
                        , .to_stop_id             = std::nullopt
                        , .to_position            = std::nullopt
                        , .departure              = std::nullopt
                        , .arrival                = std::nullopt
                        , .passengers             = std::nullopt
                        , .passenger_segments     = load.passenger_segments
                        , .segment_load_count     = load.segment_load_count
                    }
                );
            }

            for (const auto& load : loads.route_loads) {
                projection.load_rows.push_back(
                    AssignmentLoadCsvRow{
                          .level                  = AssignmentLoadLevel::Route
                        , .interval_id            = load.interval
                        , .line_id                = load.line
                        , .route_id               = load.route
                        , .trip_id                = std::nullopt
                        , .route_segment_id       = std::nullopt
                        , .connection_segment_id  = std::nullopt
                        , .from_stop_id           = std::nullopt
                        , .from_position          = std::nullopt
                        , .to_stop_id             = std::nullopt
                        , .to_position            = std::nullopt
                        , .departure              = std::nullopt
                        , .arrival                = std::nullopt
                        , .passengers             = std::nullopt
                        , .passenger_segments     = load.passenger_segments
                        , .segment_load_count     = load.segment_load_count
                    }
                );
            }

            for (const auto& load : loads.trip_loads) {
                projection.load_rows.push_back(
                    AssignmentLoadCsvRow{
                          .level                  = AssignmentLoadLevel::Trip
                        , .interval_id            = load.interval
                        , .line_id                = load.line
                        , .route_id               = load.route
                        , .trip_id                = load.trip
                        , .route_segment_id       = std::nullopt
                        , .connection_segment_id  = std::nullopt
                        , .from_stop_id           = std::nullopt
                        , .from_position          = std::nullopt
                        , .to_stop_id             = std::nullopt
                        , .to_position            = std::nullopt
                        , .departure              = std::nullopt
                        , .arrival                = std::nullopt
                        , .passengers             = std::nullopt
                        , .passenger_segments     = load.passenger_segments
                        , .segment_load_count     = load.segment_load_count
                    }
                );
            }

            for (const auto& load : loads.segment_loads) {
                projection.load_rows.push_back(
                    AssignmentLoadCsvRow{
                          .level                  = AssignmentLoadLevel::Segment
                        , .interval_id            = load.interval
                        , .line_id                = load.line
                        , .route_id               = load.route
                        , .trip_id                = load.trip
                        , .route_segment_id       = load.route_segment
                        , .connection_segment_id  = load.connection_segment
                        , .from_stop_id           = load.from.stop
                        , .from_position          = load.from.position
                        , .to_stop_id             = load.to.stop
                        , .to_position            = load.to.position
                        , .departure              = load.departure
                        , .arrival                = load.arrival
                        , .passengers             = load.passengers
                        , .passenger_segments     = std::nullopt
                        , .segment_load_count     = 1
                    }
                );
            }
        }

        void append_stop_load_projection_rows(
              AssignmentCsvProjection& projection
            , const AssignmentLoads&   loads
        ) {
            projection.stop_load_rows.reserve(loads.stop_loads.size());
            for (const auto& load : loads.stop_loads) {
                projection.stop_load_rows.push_back(
                    AssignmentStopLoadCsvRow{
                          .interval_id                      = load.interval
                        , .stop_id                          = load.stop
                        , .boarding_passengers              = load.boarding_passengers
                        , .alighting_passengers             = load.alighting_passengers
                        , .transfer_boarding_passengers     = load.transfer_boarding_passengers
                        , .transfer_alighting_passengers    = load.transfer_alighting_passengers
                        , .incoming_passenger_segments      = load.incoming_passenger_segments
                        , .outgoing_passenger_segments      = load.outgoing_passenger_segments
                        , .through_passengers               = load.through_passengers
                        , .stop_turnover_passengers         =
                              load.boarding_passengers + load.alighting_passengers
                        , .transfer_passengers              =
                              load.transfer_boarding_passengers
                            + load.transfer_alighting_passengers
                    }
                );
            }
        }

        void append_elementary_segment_load_projection_rows(
              AssignmentCsvProjection&       projection
            , const ElementarySegmentLoads&  loads
        ) {
            projection.elementary_segment_load_rows.reserve(loads.items.size());
            for (const auto& load : loads.items) {
                projection.elementary_segment_load_rows.push_back(
                    AssignmentElementarySegmentLoadCsvRow{
                          .interval_id = load.key.interval
                        , .trip_id     = load.key.item.trip
                        , .from_index  = load.key.item.from_index
                        , .passengers  = load.passengers
                    }
                );
            }
        }

        void append_skim_matrix_projection_rows(
              AssignmentCsvProjection&      projection
            , const AssignmentSkimMatrix&   skim_matrix
        ) {
            projection.skim_matrix_rows.reserve(skim_matrix.entries.size());
            for (const auto& entry : skim_matrix.entries) {
                projection.skim_matrix_rows.push_back(
                    AssignmentSkimMatrixCsvRow{
                          .origin                    = entry.origin
                        , .destination               = entry.destination
                        , .interval_id               = entry.interval
                        , .demand_passengers         = entry.demand_passengers
                        , .assigned_passengers       = entry.assigned_passengers
                        , .connection_count          = entry.connection_count
                        , .included_connection_count = entry.included_connection_count
                        , .journey_time              = entry.journey_time
                        , .in_vehicle_time           = entry.in_vehicle_time
                        , .access_time               = entry.access_time
                        , .egress_time               = entry.egress_time
                        , .walk_time                 = entry.walk_time
                        , .wait_time                 = entry.wait_time
                        , .transfer_wait_time        = entry.transfer_wait_time
                        , .transfer_walk_time        = entry.transfer_walk_time
                        , .transfers                 = entry.transfers
                        , .fare                      = entry.fare
                        , .split_impedance           = entry.split_impedance
                    }
                );
            }
        }

        void append_vehicle_journey_item_load_projection_rows(
              AssignmentCsvProjection&                    projection
            , const VehicleJourneyItemOverloadAssessment& vehicle_journey_item_loads
        ) {
            projection.vehicle_journey_item_load_rows.reserve(
                vehicle_journey_item_loads.items.size()
            );
            for (const auto& item : vehicle_journey_item_loads.items) {
                projection.vehicle_journey_item_load_rows.push_back(
                    AssignmentVehicleJourneyItemLoadCsvRow{
                          .interval_id          = item.key.interval
                        , .trip_id              = item.key.item.trip
                        , .from_index           = item.key.item.from_index
                        , .passengers           = item.passengers
                        , .total_capacity       = item.total_capacity
                        , .seat_capacity        = item.seat_capacity
                        , .load_factor          = item.load_factor
                        , .overload_passengers  = item.overload_passengers
                        , .status               = item.status
                    }
                );
            }
        }

        mathfp::Expected<mathfp::Unit> append_od_projection_rows(
              AssignmentCsvProjection&   projection
            , const AssignmentOdResult&  od_result
            , const AssignmentOdSummary& od_summary
        ) {
            MATHFP_TRY(ensure_matching_od_summary(od_result, od_summary));
            projection.od_summary_rows.push_back(build_od_summary_row(od_summary));
            MATHFP_TRY(append_connection_projection_rows(projection, od_result, od_summary));
            append_share_projection_rows(projection, od_result);
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<AssignmentCsvProjection> build_assignment_csv_projection(
        const AssignmentOutput& output
    ) {
        MATHFP_TRY_LET(
              AssignmentResultSummary
            , summary
            , build_assignment_result_summary(output)
        );

        if (summary.od_results.size() != output.od_results.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("assignment csv projection encountered mismatched OD result count")
                    .ctx("output_od_count" , static_cast<std::int64_t>(output .od_results.size()))
                    .ctx("summary_od_count", static_cast<std::int64_t>(summary.od_results.size()))
            );
        }

        const auto segment_row_count = count_total_segment_rows(output);
        const auto load_row_count    = count_total_load_rows(output);
        const auto vehicle_journey_item_load_row_count =
            count_total_vehicle_journey_item_load_rows(output);

        AssignmentCsvProjection projection{};
        projection.metadata_rows.push_back(build_metadata_row(output));
        projection.od_summary_rows.reserve(summary.od_results.size());
        projection.connection_rows.reserve(chosen_alternative_count(output.summary));
        projection.share_rows     .reserve(output.summary.demand_share_count);
        projection.segment_rows   .reserve(segment_row_count);
        projection.load_rows      .reserve(load_row_count);
        projection.stop_load_rows .reserve(output.loads.stop_loads.size());
        projection.elementary_segment_load_rows.reserve(
            output.elementary_segment_loads.items.size()
        );
        projection.skim_matrix_rows.reserve(output.skim_matrix.entries.size());
        projection.vehicle_journey_item_load_rows.reserve(vehicle_journey_item_load_row_count);

        for (std::size_t od_index = 0; od_index < output.od_results.size(); ++od_index) {
            const auto& od_result  = output .od_results[od_index];
            const auto& od_summary = summary.od_results[od_index];
            MATHFP_TRY(append_od_projection_rows(projection, od_result, od_summary));
        }
        append_load_projection_rows(projection, output.loads);
        append_stop_load_projection_rows(projection, output.loads);
        append_elementary_segment_load_projection_rows(
              projection
            , output.elementary_segment_loads
        );
        append_skim_matrix_projection_rows(projection, output.skim_matrix);
        append_vehicle_journey_item_load_projection_rows(
              projection
            , output.vehicle_journey_item_loads
        );

        return projection;
    }

}  // namespace timetable::domain::assignment::projection
