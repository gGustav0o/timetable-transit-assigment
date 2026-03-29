#pragma once

#include <filesystem>

#include <mathfp/core/expected.hpp>

#include "timetable/infra/segment_columns.hpp"

namespace timetable::infra::csv {

    /**
     * @brief Parse pair-layout CSV segment input into SegmentColumns.
     */
    mathfp::Expected<timetable::infra::SegmentColumns> parse_connection_segments_csv(
        const std::filesystem::path& path
    );

}  // namespace timetable::infra::csv
