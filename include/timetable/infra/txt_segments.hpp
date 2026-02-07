#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::infra::txt {

	struct SegmentColumns final {
		std::vector<std::int64_t> from_zone_id{};
		std::vector<std::int64_t> from_stop_id{};
		std::vector<std::int64_t> to_zone_id{};
		std::vector<std::int64_t> to_stop_id{};
		std::vector<std::int64_t> profile_id{};
		std::vector<double>       length_km{};
		std::vector<double>       time_sec{};
		std::vector<double>       dep_sec{};
		std::vector<double>       arr_sec{};
		std::vector<double>       fare{};
		std::vector<std::int64_t> zone_ids{};
	};

	mathfp::Expected<SegmentColumns> parse_segments_file(
		const std::filesystem::path& path
	);

	mathfp::Expected<timetable::domain::AssignmentInput> build_assignment_input(
		SegmentColumns columns
	);

}  // namespace timetable::infra::txt
