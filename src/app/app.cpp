#include "timetable/app/app.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include "timetable/app/error_format.hpp"
#include "timetable/domain/assignment/run.hpp"
#include "timetable/infra/logging.hpp"
#include "timetable/ui/ui.hpp"

#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>
#include <spdlog/spdlog.h>

namespace timetable::app {

	mathfp::Expected<mathfp::Unit> run(
		const AppConfig& config
		, const io::DataSource& data_source
	) {
		(void)config;
		(void)data_source;

		auto logging = timetable::infra::init_logging(config.log_capacity, config.log_dir);
		auto logger = logging.logger;

		auto load_result = data_source.load() |
			mathfp::fp::pipe::inspect_error([&](const auto& err) {
				if (logger)
					logger->error("input load failed:\n{}" , timetable::app::format_error(err));
			});

		MATHFP_TRY(load_result);
		const auto assignment_result
			= timetable::domain::assignment::run_timetable_assignment(load_result.value())
			| mathfp::fp::pipe::inspect_error([&](const auto& err) {
				if (logger)
					logger->error("assignment failed:\n{}" , timetable::app::format_error(err));
			});

		ui::UiModel model;
		if (!assignment_result) {
			model.set_status_lines({
				"assignment failed"
				, timetable::app::format_error(assignment_result.error())
			});
		} else {
			model.set_status_lines({ "ready" });
		}
		model.set_log_lines(logging.log_buffer->snapshot());

		std::atomic_bool running = true;
		std::thread log_thread([&] {
			while (running.load(std::memory_order_relaxed)) {
				model.set_log_lines(logging.log_buffer->snapshot());
				std::this_thread::sleep_for(config.log_refresh_interval);
			}
		});

		auto ui_result = ui::run(model, logger);
		running.store(false, std::memory_order_relaxed);
		if (log_thread.joinable()) {
			log_thread.join();
		}
		return ui_result;
	}

}  // namespace timetable::app
