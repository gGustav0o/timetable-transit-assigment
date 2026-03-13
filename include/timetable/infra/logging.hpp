#pragma once

#include <filesystem>
#include <memory>

#include "timetable/infra/log_buffer.hpp"

namespace spdlog {
	class logger;
}  // namespace spdlog

namespace timetable::infra {

	struct LoggingContext {
		std::shared_ptr<spdlog::logger> logger;
		std::shared_ptr<LogBuffer>      log_buffer;
	};

	mathfp::Expected<LoggingContext> init_logging(
		std::size_t log_capacity
		, const std::filesystem::path& log_dir
		, bool enable_console_sink = true
	);

	bool logging_started() noexcept;

}  // namespace timetable::infra
