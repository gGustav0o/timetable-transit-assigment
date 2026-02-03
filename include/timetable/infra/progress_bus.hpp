#pragma once

#include <functional>
#include <string_view>

#include "timetable/infra/log_level.hpp"

namespace timetable::infra::progress {

	using Sink = std::function<void(LogLevel, std::string_view)>;

	void set_status_sink(Sink sink);
	void set_log_sink(Sink sink);
	void clear_sinks();

	void status(std::string_view message, LogLevel level = LogLevel::Info);
	void log(std::string_view message, LogLevel level = LogLevel::Info);
	void both(std::string_view message, LogLevel level = LogLevel::Info);

}  // namespace timetable::infra::progress
