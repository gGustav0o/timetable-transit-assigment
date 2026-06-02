#include "timetable/infra/assignment_output_files.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/traverse.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include <fmt/format.h>

#include "timetable/domain/assignment/projection/csv.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "timetable/infra/assignment_output_csv.hpp"
#include "timetable/infra/assignment_output_json.hpp"
#include "timetable/infra/assignment_output_text.hpp"

namespace timetable::infra {
    namespace {

        namespace projection = timetable::domain::assignment::projection;

        struct PreparedArtifact final {
            std::string_view      kind{};
            std::filesystem::path path{};
            std::string           contents{};
        };

        using PreparedArtifacts = std::array<PreparedArtifact, 12>;
        constexpr std::size_t kFullPathLevelExportConnectionLimit = 500000u;

        std::filesystem::path sibling_results_dir(
            const std::filesystem::path& log_dir
        ) {
            if (const auto parent = log_dir.parent_path(); !parent.empty()) {
                return parent / "results";
            }
            return std::filesystem::path{ "results" };
        }

        mathfp::Expected<mathfp::Unit> ensure_directory_ready(
            const std::filesystem::path& path
        ) {
            std::error_code ec;
            std::filesystem::create_directories(path, ec);
            if (ec) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed to create assignment output directory")
                        .ctx("path"        , path.string())
                        .ctx("error_code"  , static_cast<std::int64_t>(ec.value()))
                        .ctx("system_error", ec.message())
                );
            }

            if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("assignment output path is not a directory")
                        .ctx("path", path.string())
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> write_text_file(
              std::string_view             contents
            , const std::filesystem::path& path
        ) {
            std::ofstream stream(path, std::ios::binary | std::ios::trunc);
            if (!stream.is_open()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed to open assignment output file for writing")
                        .ctx("path", path.string())
                );
            }

            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!stream.good()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed while writing assignment output file")
                        .ctx("path", path.string())
                );
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<PreparedArtifact> prepare_summary_artifact(
              const timetable::domain::AssignmentOutput& output
            , const std::filesystem::path&               path
        ) {
            MATHFP_TRY_LET(std::string, summary_text, serialize_assignment_output_summary_text(output));
            return PreparedArtifact{
                  .kind     = "summary_text"
                , .path     = path
                , .contents = std::move(summary_text)
            };
        }

        mathfp::Expected<PreparedArtifact> prepare_json_artifact(
              const timetable::domain::AssignmentOutput& output
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "canonical_json"
                , .path     = path
                , .contents = serialize_assignment_output_json(output)
            };
        }

        PreparedArtifact prepare_metadata_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "metadata_csv"
                , .path     = path
                , .contents = serialize_assignment_metadata_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_od_summary_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "od_summary_csv"
                , .path     = path
                , .contents = serialize_assignment_od_summary_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_connections_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "connections_csv"
                , .path     = path
                , .contents = serialize_assignment_connections_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_shares_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "shares_csv"
                , .path     = path
                , .contents = serialize_assignment_shares_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_segments_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "segments_csv"
                , .path     = path
                , .contents = serialize_assignment_segments_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_loads_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "loads_csv"
                , .path     = path
                , .contents = serialize_assignment_loads_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_stop_loads_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "stop_loads_csv"
                , .path     = path
                , .contents = serialize_assignment_stop_loads_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_elementary_segment_loads_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "elementary_segment_loads_csv"
                , .path     = path
                , .contents = serialize_assignment_elementary_segment_loads_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_skim_matrix_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "skim_matrix_csv"
                , .path     = path
                , .contents = serialize_assignment_skim_matrix_csv(csv_projection)
            };
        }

        PreparedArtifact prepare_vehicle_journey_item_loads_csv_artifact(
              const projection::AssignmentCsvProjection& csv_projection
            , const std::filesystem::path&               path
        ) {
            return PreparedArtifact{
                  .kind     = "vehicle_journey_item_loads_csv"
                , .path     = path
                , .contents = serialize_assignment_vehicle_journey_item_loads_csv(csv_projection)
            };
        }

        mathfp::Expected<PreparedArtifacts> prepare_assignment_artifacts(
              const timetable::domain::AssignmentOutput& output
            , const projection::AssignmentCsvProjection& csv_projection
            , const AssignmentOutputPaths&               paths
        ) {
            MATHFP_TRY_LET(PreparedArtifact, summary_text, prepare_summary_artifact(output, paths.summary_text_path));
            MATHFP_TRY_LET(PreparedArtifact, canonical_json, prepare_json_artifact(output, paths.canonical_json_path));

            return std::array{
                  std::move(summary_text)
                , std::move(canonical_json)
                , prepare_metadata_csv_artifact(csv_projection, paths.metadata_csv_path)
                , prepare_od_summary_csv_artifact(csv_projection, paths.od_summary_csv_path)
                , prepare_connections_csv_artifact(csv_projection, paths.connections_csv_path)
                , prepare_shares_csv_artifact(csv_projection, paths.shares_csv_path)
                , prepare_segments_csv_artifact(csv_projection, paths.segments_csv_path)
                , prepare_loads_csv_artifact(csv_projection, paths.loads_csv_path)
                , prepare_stop_loads_csv_artifact(csv_projection, paths.stop_loads_csv_path)
                , prepare_elementary_segment_loads_csv_artifact(
                      csv_projection
                    , paths.elementary_segment_loads_csv_path
                  )
                , prepare_skim_matrix_csv_artifact(csv_projection, paths.skim_matrix_csv_path)
                , prepare_vehicle_journey_item_loads_csv_artifact(
                      csv_projection
                    , paths.vehicle_journey_item_loads_csv_path
                  )
            };
        }

        mathfp::Expected<mathfp::Unit> write_prepared_artifact(
            const PreparedArtifact& artifact
        ) {
            auto written = write_text_file(artifact.contents, artifact.path);
            if (!written) {
                auto err = std::move(written.error());
                err.ctx("artifact_kind", std::string(artifact.kind));
                return mathfp::unexpected(std::move(err));
            }
            return mathfp::kUnit;
        }

        bool compact_large_od_day_export_required(
            const timetable::domain::AssignmentOutput& output
        ) noexcept {
            return output.mode == timetable::domain::AssignmentOutputMode::Calculated
                && output.summary.chosen_connection_count > kFullPathLevelExportConnectionLimit;
        }

        projection::AssignmentMetadataCsvRow compact_metadata_row(
            const timetable::domain::AssignmentOutput& output
        ) {
            return projection::AssignmentMetadataCsvRow{
                  .mode                    = output.mode
                , .skim_status             = output.skim_matrix.status
                , .od_count                = output.summary.od_count
                , .search_connection_count = output.summary.search_connection_count
                , .chosen_connection_count = output.summary.chosen_connection_count
                , .demand_share_count      = output.summary.demand_share_count
                , .skim_entry_count        = output.skim_matrix.entries.size()
                , .total_demand_passengers = output.summary.total_demand_passengers
                , .assigned_passengers     = output.summary.assigned_passengers
                , .runtime_seconds         = output.summary.runtime_seconds
            };
        }

        projection::AssignmentOdSummaryCsvRow compact_od_summary_row(
            const timetable::domain::AssignmentOdResult& od_result
        ) {
            std::size_t share_count = 0u;
            for (const auto& interval : od_result.intervals) {
                share_count += interval.shares.size();
            }

            std::optional<timetable::domain::Time> fastest_journey_time;
            std::optional<double> lowest_fare;
            std::optional<timetable::domain::TransferCount> minimum_transfers;
            for (const auto& connection : od_result.connections) {
                const auto metrics = timetable::domain::assignment::metrics_of(
                    connection.summary
                );
                if (!fastest_journey_time.has_value()
                    || metrics.journey_time.value() < fastest_journey_time->value()) {
                    fastest_journey_time = metrics.journey_time;
                }
                if (!lowest_fare.has_value()
                    || metrics.fare < *lowest_fare) {
                    lowest_fare = metrics.fare;
                }
                if (!minimum_transfers.has_value()
                    || metrics.transfer_count.get() < minimum_transfers->get()) {
                    minimum_transfers = metrics.transfer_count;
                }
            }

            return projection::AssignmentOdSummaryCsvRow{
                  .origin                   = od_result.origin
                , .destination              = od_result.destination
                , .search_connection_count  = od_result.search_connection_count
                , .chosen_connection_count  = od_result.chosen_connection_count
                , .interval_count           = od_result.intervals.size()
                , .share_count              = share_count
                , .total_demand_passengers  = od_result.total_demand_passengers
                , .assigned_passengers      = od_result.assigned_passengers
                , .fastest_journey_time     = fastest_journey_time
                , .lowest_fare              = lowest_fare
                , .minimum_transfers        = minimum_transfers
            };
        }

        void append_compact_od_rows(
              projection::AssignmentCsvProjection& projection
            , const timetable::domain::AssignmentOutput& output
        ) {
            projection.od_summary_rows.reserve(output.od_results.size());
            projection.share_rows.reserve(output.summary.demand_share_count);
            for (const auto& od_result : output.od_results) {
                projection.od_summary_rows.push_back(compact_od_summary_row(od_result));
                for (const auto& interval : od_result.intervals) {
                    for (const auto& share : interval.shares) {
                        projection.share_rows.push_back(
                            projection::AssignmentShareCsvRow{
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
            }
        }

        void append_compact_load_rows(
              projection::AssignmentCsvProjection& projection
            , const timetable::domain::AssignmentLoads& loads
        ) {
            projection.load_rows.reserve(
                  loads.line_loads.size()
                + loads.route_loads.size()
                + loads.trip_loads.size()
                + loads.segment_loads.size()
            );
            for (const auto& load : loads.line_loads) {
                projection.load_rows.push_back(
                    projection::AssignmentLoadCsvRow{
                          .level              = projection::AssignmentLoadLevel::Line
                        , .interval_id        = load.interval
                        , .line_id            = load.line
                        , .passenger_segments = load.passenger_segments
                        , .segment_load_count = load.segment_load_count
                    }
                );
            }
            for (const auto& load : loads.route_loads) {
                projection.load_rows.push_back(
                    projection::AssignmentLoadCsvRow{
                          .level              = projection::AssignmentLoadLevel::Route
                        , .interval_id        = load.interval
                        , .line_id            = load.line
                        , .route_id           = load.route
                        , .passenger_segments = load.passenger_segments
                        , .segment_load_count = load.segment_load_count
                    }
                );
            }
            for (const auto& load : loads.trip_loads) {
                projection.load_rows.push_back(
                    projection::AssignmentLoadCsvRow{
                          .level              = projection::AssignmentLoadLevel::Trip
                        , .interval_id        = load.interval
                        , .line_id            = load.line
                        , .route_id           = load.route
                        , .trip_id            = load.trip
                        , .passenger_segments = load.passenger_segments
                        , .segment_load_count = load.segment_load_count
                    }
                );
            }
            for (const auto& load : loads.segment_loads) {
                projection.load_rows.push_back(
                    projection::AssignmentLoadCsvRow{
                          .level                 = projection::AssignmentLoadLevel::Segment
                        , .interval_id           = load.interval
                        , .line_id               = load.line
                        , .route_id              = load.route
                        , .trip_id               = load.trip
                        , .route_segment_id      = load.route_segment
                        , .connection_segment_id = load.connection_segment
                        , .from_stop_id          = load.from.stop
                        , .from_position         = load.from.position
                        , .to_stop_id            = load.to.stop
                        , .to_position           = load.to.position
                        , .departure             = load.departure
                        , .arrival               = load.arrival
                        , .passengers            = load.passengers
                        , .segment_load_count    = 1u
                    }
                );
            }
        }

        void append_compact_stop_load_rows(
              projection::AssignmentCsvProjection& projection
            , const timetable::domain::AssignmentLoads& loads
        ) {
            projection.stop_load_rows.reserve(loads.stop_loads.size());
            for (const auto& load : loads.stop_loads) {
                projection.stop_load_rows.push_back(
                    projection::AssignmentStopLoadCsvRow{
                          .interval_id                   = load.interval
                        , .stop_id                       = load.stop
                        , .boarding_passengers           = load.boarding_passengers
                        , .alighting_passengers          = load.alighting_passengers
                        , .transfer_boarding_passengers  = load.transfer_boarding_passengers
                        , .transfer_alighting_passengers = load.transfer_alighting_passengers
                        , .incoming_passenger_segments   = load.incoming_passenger_segments
                        , .outgoing_passenger_segments   = load.outgoing_passenger_segments
                        , .through_passengers            = load.through_passengers
                        , .stop_turnover_passengers      =
                              load.boarding_passengers + load.alighting_passengers
                        , .transfer_passengers           =
                              load.transfer_boarding_passengers
                            + load.transfer_alighting_passengers
                    }
                );
            }
        }

        void append_compact_elementary_load_rows(
              projection::AssignmentCsvProjection& projection
            , const timetable::domain::assignment::ElementarySegmentLoads& loads
        ) {
            projection.elementary_segment_load_rows.reserve(loads.items.size());
            for (const auto& load : loads.items) {
                projection.elementary_segment_load_rows.push_back(
                    projection::AssignmentElementarySegmentLoadCsvRow{
                          .interval_id = load.key.interval
                        , .trip_id     = load.key.item.trip
                        , .from_index  = load.key.item.from_index
                        , .passengers  = load.passengers
                    }
                );
            }
        }

        void append_compact_skim_rows(
              projection::AssignmentCsvProjection& projection
            , const timetable::domain::assignment::AssignmentSkimMatrix& skim_matrix
        ) {
            projection.skim_matrix_rows.reserve(skim_matrix.entries.size());
            for (const auto& entry : skim_matrix.entries) {
                projection.skim_matrix_rows.push_back(
                    projection::AssignmentSkimMatrixCsvRow{
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

        void append_compact_vehicle_journey_item_rows(
              projection::AssignmentCsvProjection& projection
            , const timetable::domain::assignment::VehicleJourneyItemOverloadAssessment& loads
        ) {
            projection.vehicle_journey_item_load_rows.reserve(loads.items.size());
            for (const auto& item : loads.items) {
                projection.vehicle_journey_item_load_rows.push_back(
                    projection::AssignmentVehicleJourneyItemLoadCsvRow{
                          .interval_id         = item.key.interval
                        , .trip_id             = item.key.item.trip
                        , .from_index          = item.key.item.from_index
                        , .passengers          = item.passengers
                        , .total_capacity      = item.total_capacity
                        , .seat_capacity       = item.seat_capacity
                        , .load_factor         = item.load_factor
                        , .overload_passengers = item.overload_passengers
                        , .status              = item.status
                    }
                );
            }
        }

        projection::AssignmentCsvProjection build_compact_large_od_day_csv_projection(
            const timetable::domain::AssignmentOutput& output
        ) {
            projection::AssignmentCsvProjection projection{};
            projection.metadata_rows.push_back(compact_metadata_row(output));
            append_compact_od_rows(projection, output);
            append_compact_load_rows(projection, output.loads);
            append_compact_stop_load_rows(projection, output.loads);
            append_compact_elementary_load_rows(projection, output.elementary_segment_loads);
            append_compact_skim_rows(projection, output.skim_matrix);
            append_compact_vehicle_journey_item_rows(projection, output.vehicle_journey_item_loads);
            return projection;
        }

        std::string compact_large_od_day_json(
            const timetable::domain::AssignmentOutput& output
        ) {
            return fmt::format(
                "{{\n"
                "  \"schema\": \"timetable.assignment_output.compact_large_od_day.v1\",\n"
                "  \"export_profile\": \"compact_large_od_day\",\n"
                "  \"note\": \"Full path-level JSON is omitted for large OD-day production output; use CSV aggregate/load files.\",\n"
                "  \"summary\": {{\n"
                "    \"od_count\": {},\n"
                "    \"search_connection_count\": {},\n"
                "    \"chosen_connection_count\": {},\n"
                "    \"demand_share_count\": {},\n"
                "    \"total_demand_passengers\": {:.17g},\n"
                "    \"assigned_passengers\": {:.17g}\n"
                "  }}\n"
                "}}\n"
              , output.summary.od_count
              , output.summary.search_connection_count
              , output.summary.chosen_connection_count
              , output.summary.demand_share_count
              , output.summary.total_demand_passengers
              , output.summary.assigned_passengers
            );
        }

        std::string compact_large_od_day_summary_text(
            const timetable::domain::AssignmentOutput& output
        ) {
            return fmt::format(
                "Assignment Summary\n"
                "Mode:                calculated\n"
                "Export profile:      compact_large_od_day\n"
                "OD pairs:            {}\n"
                "Search connections:  {}\n"
                "Chosen connections:  {}\n"
                "Demand shares:       {}\n"
                "Elementary loads:    {}\n"
                "VISUM segment loads: {}\n"
                "Route totals:        {}\n"
                "Stop totals:         {}\n"
                "Overload rows:       {}\n"
                "Total demand:        {:.3f}\n"
                "Assigned passengers: {:.3f}\n"
                "Note: full path-level JSON, connections.csv, and segments.csv are omitted for this large OD-day production export.\n"
              , output.summary.od_count
              , output.summary.search_connection_count
              , output.summary.chosen_connection_count
              , output.summary.demand_share_count
              , output.elementary_segment_loads.items.size()
              , output.loads.segment_loads.size()
              , output.loads.route_total_loads.size()
              , output.loads.stop_total_loads.size()
              , output.vehicle_journey_item_loads.items.size()
              , output.summary.total_demand_passengers
              , output.summary.assigned_passengers
            );
        }

        mathfp::Expected<mathfp::Unit> write_string_artifact(
              std::string_view             kind
            , const std::filesystem::path& path
            , std::string                  contents
        ) {
            auto written = write_text_file(contents, path);
            if (!written) {
                auto err = std::move(written.error());
                err.ctx("artifact_kind", std::string(kind));
                return mathfp::unexpected(std::move(err));
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<AssignmentOutputPaths> write_compact_large_od_day_output_files(
              const timetable::domain::AssignmentOutput& output
            , const std::filesystem::path&               log_dir
        ) {
            const auto paths = resolve_assignment_output_paths(log_dir);
            MATHFP_TRY(ensure_directory_ready(paths.results_dir));

            timetable::infra::progress::log(
                fmt::format(
                      "assignment output export: profile=compact_large_od_day chosen_connections={} threshold={} full_path_json=omitted connections_csv=header_only segments_csv=header_only"
                    , output.summary.chosen_connection_count
                    , kFullPathLevelExportConnectionLimit
                )
            );

            MATHFP_TRY(write_string_artifact(
                  "summary_text"
                , paths.summary_text_path
                , compact_large_od_day_summary_text(output)
            ));
            MATHFP_TRY(write_string_artifact(
                  "canonical_json"
                , paths.canonical_json_path
                , compact_large_od_day_json(output)
            ));

            auto projection = build_compact_large_od_day_csv_projection(output);
            MATHFP_TRY(write_string_artifact("metadata_csv", paths.metadata_csv_path, serialize_assignment_metadata_csv(projection)));
            MATHFP_TRY(write_string_artifact("od_summary_csv", paths.od_summary_csv_path, serialize_assignment_od_summary_csv(projection)));
            MATHFP_TRY(write_string_artifact("connections_csv", paths.connections_csv_path, serialize_assignment_connections_csv(projection)));
            MATHFP_TRY(write_string_artifact("shares_csv", paths.shares_csv_path, serialize_assignment_shares_csv(projection)));
            MATHFP_TRY(write_string_artifact("segments_csv", paths.segments_csv_path, serialize_assignment_segments_csv(projection)));
            MATHFP_TRY(write_string_artifact("loads_csv", paths.loads_csv_path, serialize_assignment_loads_csv(projection)));
            MATHFP_TRY(write_string_artifact("stop_loads_csv", paths.stop_loads_csv_path, serialize_assignment_stop_loads_csv(projection)));
            MATHFP_TRY(write_string_artifact(
                  "elementary_segment_loads_csv"
                , paths.elementary_segment_loads_csv_path
                , serialize_assignment_elementary_segment_loads_csv(projection)
            ));
            MATHFP_TRY(write_string_artifact("skim_matrix_csv", paths.skim_matrix_csv_path, serialize_assignment_skim_matrix_csv(projection)));
            MATHFP_TRY(write_string_artifact(
                  "vehicle_journey_item_loads_csv"
                , paths.vehicle_journey_item_loads_csv_path
                , serialize_assignment_vehicle_journey_item_loads_csv(projection)
            ));

            return paths;
        }

    }  // namespace

    AssignmentOutputPaths resolve_assignment_output_paths(
        const std::filesystem::path& log_dir
    ) {
        const auto results_dir = sibling_results_dir(log_dir);
        return AssignmentOutputPaths{
              .results_dir          = results_dir
            , .summary_text_path    = results_dir / "assignment_summary.txt"
            , .canonical_json_path  = results_dir / "assignment_output.json"
            , .metadata_csv_path    = results_dir / "metadata.csv"
            , .od_summary_csv_path  = results_dir / "od_summary.csv"
            , .connections_csv_path = results_dir / "connections.csv"
            , .shares_csv_path      = results_dir / "shares.csv"
            , .segments_csv_path    = results_dir / "segments.csv"
            , .loads_csv_path       = results_dir / "loads.csv"
            , .stop_loads_csv_path  = results_dir / "stop_loads.csv"
            , .elementary_segment_loads_csv_path =
                  results_dir / "elementary_segment_loads.csv"
            , .skim_matrix_csv_path = results_dir / "skim_matrix.csv"
            , .vehicle_journey_item_loads_csv_path =
                  results_dir / "vehicle_journey_item_loads.csv"
        };
    }

    mathfp::Expected<AssignmentOutputPaths> write_assignment_output_files(
          const timetable::domain::AssignmentOutput& output
        , const std::filesystem::path&               log_dir
    ) {
        if (compact_large_od_day_export_required(output)) {
            return write_compact_large_od_day_output_files(output, log_dir);
        }

        MATHFP_TRY_LET(
              projection::AssignmentCsvProjection
            , csv_projection
            , projection::build_assignment_csv_projection(output)
        );

        const auto paths = resolve_assignment_output_paths(log_dir);
        MATHFP_TRY(ensure_directory_ready(paths.results_dir));

        MATHFP_TRY_LET(
              PreparedArtifacts
            , artifacts
            , prepare_assignment_artifacts(output, csv_projection, paths)
        );
        MATHFP_TRY(mathfp::trv::traverse(
              artifacts
            , [](const PreparedArtifact& artifact) {
                return write_prepared_artifact(artifact);
            }
        ));

        return paths;
    }

}  // namespace timetable::infra
