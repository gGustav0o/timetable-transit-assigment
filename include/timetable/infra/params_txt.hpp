#pragma once

#include <filesystem>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/runtime_params.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::infra::params_txt {

    mathfp::Expected<timetable::domain::SearchParams> parse_search_params_file(
        const std::filesystem::path& path
    );

    mathfp::Expected<timetable::domain::AssignmentRuntimeParams> parse_assignment_runtime_params_file(
        const std::filesystem::path& path
    );

}  // namespace timetable::infra::params_txt
