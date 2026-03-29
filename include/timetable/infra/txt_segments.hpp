#pragma once

#include <filesystem>

#include <mathfp/core/expected.hpp>

#include "timetable/infra/segment_columns.hpp"

namespace timetable::infra::txt {

    /**
     * @brief Parse the single-file TXT segment format into SegmentColumns.
     */
    mathfp::Expected<timetable::infra::SegmentColumns> parse_segments_file(
        const std::filesystem::path& path
    );

}  // namespace timetable::infra::txt
