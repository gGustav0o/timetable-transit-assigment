#include "timetable/app/app.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

#include "timetable/app/error_format.hpp"
#include "timetable/domain/assignment/run.hpp"
#include "timetable/infra/log_entry.hpp"
#include "timetable/infra/logging.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "timetable/ui/ui.hpp"

#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>
#include <spdlog/spdlog.h>

namespace timetable::app {
	namespace {

		spdlog::level::level_enum to_spdlog_level(
			timetable::infra::LogLevel level
		) noexcept {
			using Level = timetable::infra::LogLevel;
			switch (level) {
				case Level::Info:
					return spdlog::level::info;
				case Level::Warning:
					return spdlog::level::warn;
				case Level::Error:
					return spdlog::level::err;
				case Level::Debug:
					return spdlog::level::debug;
			}
			return spdlog::level::info;
		}

	}  // namespace

	mathfp::Expected<mathfp::Unit> run(
		const AppConfig& config
		, const io::DataSource& data_source
	) {
		(void)config;
		(void)data_source;

		auto logging = timetable::infra::init_logging(config.log_capacity, config.log_dir);
		auto logger = logging.logger;

		ui::UiModel model;
		timetable::infra::progress::set_log_sink(
			[logger](timetable::infra::LogLevel level, std::string_view message) {
			if (logger) {
				logger->log(to_spdlog_level(level), "{}", message);
			}
		});
		timetable::infra::progress::set_status_sink(
			[&model](timetable::infra::LogLevel level, std::string_view message) {
				model.set_status_lines({
					timetable::infra::LogEntry{ level, std::string(message) }
				});
			});

		timetable::infra::progress::status("waiting to start");
		model.set_log_lines(logging.log_buffer->snapshot());

		std::atomic_bool running = true;
		std::thread log_thread([&] {
			while (running.load(std::memory_order_relaxed)) {
				model.set_log_lines(logging.log_buffer->snapshot());
				std::this_thread::sleep_for(config.log_refresh_interval);
			}
		});

		std::thread worker_thread([&] {
			auto load_result = data_source.load() |
				mathfp::fp::pipe::inspect_error([&](const auto& err) {
					if (logger)
						logger->error("input load failed:\n{}" , timetable::app::format_error(err));
				});

			if (!load_result) {
				timetable::infra::progress::status(
					"input load failed"
					, timetable::infra::LogLevel::Error
				);
				timetable::infra::progress::status(
					timetable::app::format_error(load_result.error())
					, timetable::infra::LogLevel::Error
				);
				return;
			}

			const auto assignment_result
				= timetable::domain::assignment::run_timetable_assignment(load_result.value())
				| mathfp::fp::pipe::inspect_error([&](const auto& err) {
					if (logger)
						logger->error("assignment failed:\n{}" , timetable::app::format_error(err));
				});

			if (!assignment_result) {
				timetable::infra::progress::status(
					"assignment failed"
					, timetable::infra::LogLevel::Error
				);
				timetable::infra::progress::status(
					timetable::app::format_error(assignment_result.error())
					, timetable::infra::LogLevel::Error
				);
			} else {
				timetable::infra::progress::status("ready");
			}
		});

		auto ui_result = ui::run(model, logger);
		running.store(false, std::memory_order_relaxed);
		if (log_thread.joinable()) {
			log_thread.join();
		}
		if (worker_thread.joinable()) {
			worker_thread.join();
		}
		timetable::infra::progress::clear_sinks();
		return ui_result;
	}

}  // namespace timetable::app
