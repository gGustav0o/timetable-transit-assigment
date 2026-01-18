#pragma once

#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/ui/model.hpp"

namespace spdlog {
	class logger;
}  // namespace spdlog

namespace timetable::ui {

	mathfp::Expected<mathfp::Unit> run(
		const UiModel& model
		, const std::shared_ptr<spdlog::logger>& logger
	);

}  // namespace timetable::ui
