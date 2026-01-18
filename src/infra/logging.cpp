#include "timetable/infra/logging.hpp"

#include <atomic>
#include <filesystem>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "timetable/infra/ui_log_sink.hpp"

namespace timetable::infra {
	namespace {
		std::atomic_bool g_logging_started = false;
	}

	LoggingContext init_logging(
		std::size_t log_capacity
		, const std::filesystem::path& log_dir
	) {
		auto log_buffer = std::make_shared<LogBuffer>(log_capacity);
		auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		std::filesystem::create_directories(log_dir);
		auto log_path = log_dir / "timetable.log";
		auto file_sink
			= std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);
		auto ui_sink = std::make_shared<UiLogSink>(log_buffer);
		auto logger = std::make_shared<spdlog::logger>(
			"timetable"
			, spdlog::sinks_init_list{ console_sink, file_sink, ui_sink }
		);
		spdlog::set_default_logger(logger);
		spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
		g_logging_started.store(true, std::memory_order_relaxed);
		return LoggingContext{ logger, log_buffer };
	}

	bool logging_started() noexcept {
		return g_logging_started.load(std::memory_order_relaxed);
	}

}  // namespace timetable::infra
