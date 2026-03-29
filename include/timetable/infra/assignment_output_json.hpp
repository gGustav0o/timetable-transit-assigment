#pragma once

#include <filesystem>
#include <string>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::infra {

    /**
     * @brief Serialize the canonical assignment result to a lossless JSON document.
     *
     * The JSON preserves the full AssignmentOutput structure and is intended as
     * the canonical machine-readable export format.
     */
    std::string serialize_assignment_output_json(
        const timetable::domain::AssignmentOutput& output
    );

    /**
     * @brief Write the canonical assignment result as a lossless JSON file.
     */
    mathfp::Expected<mathfp::Unit> write_assignment_output_json(
        const timetable::domain::AssignmentOutput& output
        , const std::filesystem::path& path
    );

}  // namespace timetable::infra
