#pragma once

#include <filesystem>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::infra {

    /**
     * @brief Canonical filesystem layout for one assignment result set.
     *
     * Paths are resolved in the results directory located next to the configured
     * log directory.
     */
    struct AssignmentOutputPaths final {
        std::filesystem::path results_dir{};
        std::filesystem::path summary_text_path{};
        std::filesystem::path canonical_json_path{};
        std::filesystem::path metadata_csv_path{};
        std::filesystem::path od_summary_csv_path{};
        std::filesystem::path connections_csv_path{};
        std::filesystem::path shares_csv_path{};
        std::filesystem::path segments_csv_path{};
        std::filesystem::path loads_csv_path{};
        std::filesystem::path stop_loads_csv_path{};
        std::filesystem::path skim_matrix_csv_path{};
        std::filesystem::path vehicle_journey_item_loads_csv_path{};
    };

    /**
     * @brief Resolve canonical output file paths for a configured log directory.
     */
    AssignmentOutputPaths resolve_assignment_output_paths(
        const std::filesystem::path& log_dir
    );

    /**
     * @brief Write the complete assignment result artifact set.
     *
     * The current output policy always writes:
     * - assignment_summary.txt
     * - assignment_output.json
     * - metadata.csv
     * - od_summary.csv
     * - connections.csv
     * - shares.csv
     * - segments.csv
     * - loads.csv
     * - stop_loads.csv
     * - skim_matrix.csv
     * - vehicle_journey_item_loads.csv
     *
     * All files are written into the results directory located next to the
     * configured log directory.
     */
    mathfp::Expected<AssignmentOutputPaths> write_assignment_output_files(
          const timetable::domain::AssignmentOutput& output
        , const std::filesystem::path&               log_dir
    );

}  // namespace timetable::infra
