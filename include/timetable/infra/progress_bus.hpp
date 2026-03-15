#pragma once

#include <functional>
#include <string_view>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/infra/log_level.hpp"

namespace timetable::infra::progress {

	using Sink = std::function<void(LogLevel, std::string_view)>;

	struct SinkState final {
		Sink status{};
		Sink log{};
	};

	mathfp::Expected<mathfp::Unit> set_sinks(SinkState sinks);
	mathfp::Expected<mathfp::Unit> set_status_sink(Sink sink);
	mathfp::Expected<mathfp::Unit> set_log_sink(Sink sink);
	mathfp::Expected<mathfp::Unit> clear_sinks();

	void status(std::string_view message, LogLevel level = LogLevel::Info);
	void log(std::string_view message, LogLevel level = LogLevel::Info);
	void both(std::string_view message, LogLevel level = LogLevel::Info);

}  // namespace timetable::infra::progress
