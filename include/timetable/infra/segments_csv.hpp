#pragma once

#include <filesystem>

#include <mathfp/core/expected.hpp>

#include "timetable/infra/txt_segments.hpp"

namespace timetable::infra::csv {

	mathfp::Expected<txt::SegmentColumns> parse_connection_segments_csv(
		const std::filesystem::path& path
	);

}  // namespace timetable::infra::csv

