#include "timetable/infra/progress_bus.hpp"

#include <cassert>
#include <mutex>
#include <string_view>

namespace timetable::infra::progress {
	namespace {
		std::mutex g_mutex;
		Sink g_status_sink;
		Sink g_log_sink;
	}

	void set_status_sink(Sink sink) {
		std::lock_guard lock(g_mutex);
		g_status_sink = std::move(sink);
	}

	void set_log_sink(Sink sink) {
		std::lock_guard lock(g_mutex);
		g_log_sink = std::move(sink);
	}

	void clear_sinks() {
		std::lock_guard lock(g_mutex);
		g_status_sink = Sink{};
		g_log_sink = Sink{};
	}

	void status(std::string_view message, LogLevel level) {
		Sink sink;
		{
			std::lock_guard lock(g_mutex);
			sink = g_status_sink;
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
			sink = g_log_sink;
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
