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
            if (od_result.origin != summary.origin
                || od_result.destination != summary.destination) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment csv projection encountered mismatched OD summary ordering")
                        .ctx("od_origin"          , od_result.origin.get())
                        .ctx("od_destination"     , od_result.destination.get())
                        .ctx("summary_origin"     , summary.origin.get())
                        .ctx("summary_destination", summary.destination.get())
                );
            }

            if (od_result.connections.size() != summary.connections.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment csv projection encountered mismatched connection summary count")
                        .ctx("origin"                  , od_result.origin.get())
                        .ctx("destination"             , od_result.destination.get())
                        .ctx("connection_count"        , static_cast<std::int64_t>(od_result.connections.size()))
                        .ctx("summary_connection_count", static_cast<std::int64_t>(summary.connections.size()))
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
                , .interval_count           = summary.interval_count
                , .share_count              = summary.share_count
                , .total_demand_passengers  = summary.total_demand_passengers
                , .assigned_passengers      = summary.assigned_passengers
                , .fastest_journey_time     = summary.fastest_journey_time
                , .lowest_fare              = summary.lowest_fare
                , .minimum_transfers        = summary.minimum_transfers
                , .minimum_search_impedance = summary.minimum_search_impedance
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
                        .ctx("origin"          , od_result.origin.get())
                        .ctx("destination"     , od_result.destination.get())
                        .ctx("connection_index", raw_index)
                );
            }

            if (summary.index != expected_index) {
                return mathfp::unexpected(
                    mathfp::internal_error("assignment csv projection encountered mismatched connection index ordering")
                        .ctx("origin"                   , od_result.origin.get())
                        .ctx("destination"              , od_result.destination.get())
                        .ctx("expected_connection_index", expected_index.get())
                        .ctx("summary_connection_index" , summary.index.get())
                );
            }

            return AssignmentConnectionCsvRow{
                  .origin              = od_result.origin
                , .destination         = od_result.destination
                , .connection_index    = summary.index
                , .departure           = summary.departure
                , .arrival             = summary.arrival
                , .journey_time        = summary.journey_time
                , .transfer_time       = summary.transfer_time
                , .transfers           = summary.transfers
                , .fare                = summary.fare
                , .search_impedance    = summary.search_impedance
                , .assigned_passengers = summary.assigned_passengers
                , .share_count         = summary.share_count
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
                            , .interval_id                  = interval.interval.id
                            , .interval_start               = interval.interval.start
                            , .interval_end                 = interval.interval.end
                            , .interval_demand_passengers   = interval.demand_passengers
                            , .interval_assigned_passengers = interval.assigned_passengers
                            , .connection_index             = share.connection_index
                            , .share_passengers             = share.passengers
                            , .probability                  = share.probability
                            , .independence                 = share.independence
                            , .split_impedance              = share.split_impedance
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

        AssignmentSegmentCsvRow build_segment_row(
              const AssignmentOdResult&    od_result
            , AssignmentConnectionRef      connection_index
            , std::size_t                  path_segment_index
            , const AssignmentPathSegment& segment
        ) {
            const auto physical_from = physical_from_key(segment.route_segment);
            const auto physical_to   = physical_to_key(segment.route_segment);

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
                        , od_result.connections[connection_index]
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
                    .ctx("output_od_count" , static_cast<std::int64_t>(output.od_results.size()))
                    .ctx("summary_od_count", static_cast<std::int64_t>(summary.od_results.size()))
            );
        }

        const auto segment_row_count = count_total_segment_rows(output);

        AssignmentCsvProjection projection{};
        projection.od_summary_rows.reserve(summary.od_results.size());
        projection.connection_rows.reserve(output.summary.chosen_connection_count);
        projection.share_rows     .reserve(output.summary.demand_share_count);
        projection.segment_rows   .reserve(segment_row_count);

        for (std::size_t od_index = 0; od_index < output.od_results.size(); ++od_index) {
            const auto& od_result  = output.od_results[od_index];
            const auto& od_summary = summary.od_results[od_index];
            MATHFP_TRY(append_od_projection_rows(projection, od_result, od_summary));
        }

        return projection;
    }

}  // namespace timetable::domain::assignment::projection
