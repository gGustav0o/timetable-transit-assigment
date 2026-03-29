#include "timetable/infra/assignment_output_files.hpp"

#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/projection/csv.hpp"
#include "timetable/infra/assignment_output_csv.hpp"
#include "timetable/infra/assignment_output_json.hpp"
#include "timetable/infra/assignment_output_text.hpp"

namespace timetable::infra {
    namespace {

        namespace projection = timetable::domain::assignment::projection;

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
                        .ctx("path", path.string())
                        .ctx("error_code", static_cast<std::int64_t>(ec.value()))
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
            std::string_view contents
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

    }  // namespace

    AssignmentOutputPaths resolve_assignment_output_paths(
        const std::filesystem::path& log_dir
    ) {
        const auto results_dir = sibling_results_dir(log_dir);
        return AssignmentOutputPaths{
            .results_dir            = results_dir
            , .summary_text_path    = results_dir / "assignment_summary.txt"
            , .canonical_json_path  = results_dir / "assignment_output.json"
            , .od_summary_csv_path  = results_dir / "od_summary.csv"
            , .connections_csv_path = results_dir / "connections.csv"
            , .shares_csv_path      = results_dir / "shares.csv"
            , .segments_csv_path    = results_dir / "segments.csv"
        };
    }

    mathfp::Expected<AssignmentOutputPaths> write_assignment_output_files(
        const timetable::domain::AssignmentOutput& output
        , const std::filesystem::path& log_dir
    ) {
        MATHFP_TRY_LET(
            projection::AssignmentCsvProjection
            , csv_projection
            , projection::build_assignment_csv_projection(output)
        );

        const auto paths = resolve_assignment_output_paths(log_dir);
        MATHFP_TRY(ensure_directory_ready(paths.results_dir));

        MATHFP_TRY(write_assignment_output_json(output, paths.canonical_json_path));
        MATHFP_TRY(write_assignment_output_summary_text(output, paths.summary_text_path));
        MATHFP_TRY(write_text_file(
            serialize_assignment_od_summary_csv(csv_projection)
            , paths.od_summary_csv_path
        ));
        MATHFP_TRY(write_text_file(
            serialize_assignment_connections_csv(csv_projection)
            , paths.connections_csv_path
        ));
        MATHFP_TRY(write_text_file(
            serialize_assignment_shares_csv(csv_projection)
            , paths.shares_csv_path
        ));
        MATHFP_TRY(write_text_file(
            serialize_assignment_segments_csv(csv_projection)
            , paths.segments_csv_path
        ));

        return paths;
    }

}  // namespace timetable::infra
