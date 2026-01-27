#include "timetable/infra/logging.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <ctime>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include "timetable/infra/ui_log_sink.hpp"

namespace timetable::infra {
	namespace {
		std::atomic_bool g_logging_started = false;
	}

	LoggingContext init_logging(
		std::size_t log_capacity
		, const std::filesystem::path& log_dir
	) {
		auto log_buffer   = std::make_shared<LogBuffer>(log_capacity);
		auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

		std::filesystem::create_directories(log_dir);
		const auto now      = std::chrono::system_clock::now();
		const auto now_time = std::chrono::system_clock::to_time_t(now);

		std::tm now_tm{};
#if defined(_WIN32)
		localtime_s(&now_tm, &now_time);
#else
		localtime_r(&now_time, &now_tm);
#endif
		const auto log_name = fmt::format(
			"timetable-{:04}{:02}{:02}-{:02}{:02}{:02}.log"
			, now_tm.tm_year + 1900
			, now_tm.tm_mon + 1
			, now_tm.tm_mday
			, now_tm.tm_hour
			, now_tm.tm_min
			, now_tm.tm_sec
		);
		auto log_path = log_dir / log_name;
		auto file_sink
			= std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);
		auto ui_sink = std::make_shared<UiLogSink>(log_buffer);
		auto logger  = std::make_shared<spdlog::logger>(
			"timetable"
			, spdlog::sinks_init_list{ console_sink, file_sink, ui_sink }
		);

		spdlog::set_default_logger(logger);
		spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
		logger->info("log file: {}", log_path.string());
		g_logging_started.store(true, std::memory_order_relaxed);
		return LoggingContext{ logger, log_buffer };
	}

	bool logging_started() noexcept {
		return g_logging_started.load(std::memory_order_relaxed);
	}

}  // namespace timetable::infra
