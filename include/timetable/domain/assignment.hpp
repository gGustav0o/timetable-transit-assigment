#pragma once

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain {

	struct AssignmentInput final {
		InputModel   input{};
		SearchParams params{};
	};

	struct AssignmentOutput {};

}  // namespace timetable::domain
