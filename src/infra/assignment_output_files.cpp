#include "timetable/infra/assignment_output_files.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/traverse.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/projection/csv.hpp"
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

        using PreparedArtifacts = std::array<PreparedArtifact, 8>;

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
                , prepare_od_summary_csv_artifact(csv_projection, paths.od_summary_csv_path)
                , prepare_connections_csv_artifact(csv_projection, paths.connections_csv_path)
                , prepare_shares_csv_artifact(csv_projection, paths.shares_csv_path)
                , prepare_segments_csv_artifact(csv_projection, paths.segments_csv_path)
                , prepare_loads_csv_artifact(csv_projection, paths.loads_csv_path)
                , prepare_skim_matrix_csv_artifact(csv_projection, paths.skim_matrix_csv_path)
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

    }  // namespace

    AssignmentOutputPaths resolve_assignment_output_paths(
        const std::filesystem::path& log_dir
    ) {
        const auto results_dir = sibling_results_dir(log_dir);
        return AssignmentOutputPaths{
              .results_dir          = results_dir
            , .summary_text_path    = results_dir / "assignment_summary.txt"
            , .canonical_json_path  = results_dir / "assignment_output.json"
            , .od_summary_csv_path  = results_dir / "od_summary.csv"
            , .connections_csv_path = results_dir / "connections.csv"
            , .shares_csv_path      = results_dir / "shares.csv"
            , .segments_csv_path    = results_dir / "segments.csv"
            , .loads_csv_path       = results_dir / "loads.csv"
            , .skim_matrix_csv_path = results_dir / "skim_matrix.csv"
        };
    }

    mathfp::Expected<AssignmentOutputPaths> write_assignment_output_files(
          const timetable::domain::AssignmentOutput& output
        , const std::filesystem::path&               log_dir
    ) {
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
