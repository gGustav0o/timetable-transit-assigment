#include "timetable/infra/progress_bus.hpp"

#include <cassert>
#include <mutex>
#include <string_view>

namespace timetable::infra::progress {
	namespace {
		std::mutex g_mutex;
		SinkState g_sinks;
	}

	mathfp::Expected<mathfp::Unit> set_sinks(SinkState sinks) {
		std::lock_guard lock(g_mutex);
		g_sinks = std::move(sinks);
		return mathfp::ok();
	}

	mathfp::Expected<mathfp::Unit> set_status_sink(Sink sink) {
		std::lock_guard lock(g_mutex);
		g_sinks.status = std::move(sink);
		return mathfp::ok();
	}

	mathfp::Expected<mathfp::Unit> set_log_sink(Sink sink) {
		std::lock_guard lock(g_mutex);
		g_sinks.log = std::move(sink);
		return mathfp::ok();
	}

	mathfp::Expected<mathfp::Unit> clear_sinks() {
		return set_sinks(SinkState{});
	}

	void status(std::string_view message, LogLevel level) {
		Sink sink;
		{
			std::lock_guard lock(g_mutex);
			sink = g_sinks.status;
		}
		assert(sink && "progress::status sink not set");
		if (sink) {
			sink(level, message);
		}
	}

	void log(std::string_view message, LogLevel level) {
		Sink sink;
		{
			std::lock_guard lock(g_mutex);
			sink = g_sinks.log;
		}
		assert(sink && "progress::log sink not set");
		if (sink) {
			sink(level, message);
		}
	}

	void both(std::string_view message, LogLevel level) {
		status(message, level);
		log(message, level);
	}

}  // namespace timetable::infra::progress
