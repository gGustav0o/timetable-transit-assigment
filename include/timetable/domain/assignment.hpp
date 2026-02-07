#pragma once

#include <optional>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain {

	struct AssignmentInput final {
		struct PresegmentedInput final {
			std::vector<RouteSegment>      route_segments{};
			std::vector<ConnectionSegment> connection_segments{};
		};

		InputModel   input{};
		SearchParams params{};
		std::optional<PresegmentedInput> presegmented{};
	};

	struct AssignmentOutput {};

}  // namespace timetable::domain
