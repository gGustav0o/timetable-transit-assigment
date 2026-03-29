#include "timetable/app/app.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "timetable/app/error_format.hpp"
#include "timetable/domain/assignment/run.hpp"
#include "timetable/infra/log_entry.hpp"
#include "timetable/infra/logging.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "timetable/ui/ui.hpp"

#include <mathfp/core/fp.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <spdlog/spdlog.h>
#include <mathfp/core/error.hpp>

namespace timetable::app {
	namespace {

		spdlog::level::level_enum to_spdlog_level(
			timetable::infra::LogLevel level
		) noexcept;

		std::thread start_log_refresh_thread(
			std::atomic_bool& running
			, ui::UiModel& model
			, const timetable::app::AppConfig& config
			, const std::shared_ptr<timetable::infra::LogBuffer>& log_buffer
		);

		std::thread start_worker_thread(
			const io::DataSource& data_source
			, const std::shared_ptr<spdlog::logger>& logger
			, class WorkerResultBox& worker_result
		);

		class ScopedProgressSinks final {
		public:
			static mathfp::Expected<ScopedProgressSinks> make(
				ui::UiModel& model
				, const std::shared_ptr<spdlog::logger>& logger
			) {
				MATHFP_TRY(timetable::infra::progress::set_sinks({
					.status = [&model](timetable::infra::LogLevel level, std::string_view message) {
						model.set_status_lines({
							timetable::infra::LogEntry{ level, std::string(message) }
						});
					},
					.log = [logger](timetable::infra::LogLevel level, std::string_view message) {
						if (logger) {
							logger->log(to_spdlog_level(level), "{}", message);
						}
					}
				}));
				return ScopedProgressSinks{ true };
			}

			ScopedProgressSinks(const ScopedProgressSinks&) = delete;
			ScopedProgressSinks& operator=(const ScopedProgressSinks&) = delete;

			ScopedProgressSinks(ScopedProgressSinks&& other) noexcept
				: active_(std::exchange(other.active_, false)) {
			}

			ScopedProgressSinks& operator=(ScopedProgressSinks&& other) noexcept {
				if (this != &other) {
					if (active_) {
						(void)timetable::infra::progress::clear_sinks();
					}
					active_ = std::exchange(other.active_, false);
				}
				return *this;
			}

			~ScopedProgressSinks() {
				if (active_) {
					(void)timetable::infra::progress::clear_sinks();
				}
			}

		private:
			explicit ScopedProgressSinks(bool active) noexcept
				: active_(active) {
			}

			bool active_ = false;
		};

		class WorkerResultBox final {
		public:
			mathfp::Expected<mathfp::Unit> set(mathfp::Expected<mathfp::Unit> result) {
				std::lock_guard lock(mutex_);
				if (result_.has_value()) {
					return mathfp::unexpected(
						mathfp::internal_error("worker result already set")
					);
				}
				result_ = std::move(result);
				return mathfp::ok();
			}

			mathfp::Expected<mathfp::Unit> take() {
				std::lock_guard lock(mutex_);
				if (!result_.has_value()) {
					return mathfp::unexpected(
						mathfp::internal_error("worker result missing")
					);
				}
				auto result = std::move(*result_);
				result_.reset();
				return result;
			}

		private:
			std::mutex                                   mutex_;
			std::optional<mathfp::Expected<mathfp::Unit>> result_;
		};

		class BackgroundThreads final {
		public:
			BackgroundThreads(
				ui::UiModel& model
				, const timetable::app::AppConfig& config
				, const std::shared_ptr<timetable::infra::LogBuffer>& log_buffer
				, const io::DataSource& data_source
				, const std::shared_ptr<spdlog::logger>& logger
				, WorkerResultBox& worker_result
			) {
				log_thread_ = start_log_refresh_thread(
					running_, model, config, log_buffer
				);
				try {
					worker_thread_ = start_worker_thread(
						data_source, logger, worker_result
					);
				} catch (...) {
					running_.store(false, std::memory_order_relaxed);
					if (log_thread_.joinable()) {
						log_thread_.join();
					}
					throw;
				}
			}

			BackgroundThreads(const BackgroundThreads&) = delete;
			BackgroundThreads& operator=(const BackgroundThreads&) = delete;

			BackgroundThreads(BackgroundThreads&&) = delete;
			BackgroundThreads& operator=(BackgroundThreads&&) = delete;

			~BackgroundThreads() {
				running_.store(false, std::memory_order_relaxed);
				if (worker_thread_.joinable()) {
					worker_thread_.join();
				}
				if (log_thread_.joinable()) {
					log_thread_.join();
				}
			}

		private:
			std::atomic_bool running_ = true;
			std::thread      log_thread_;
			std::thread      worker_thread_;
		};

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

		std::thread start_log_refresh_thread(
			std::atomic_bool& running
			, ui::UiModel& model
			, const timetable::app::AppConfig& config
			, const std::shared_ptr<timetable::infra::LogBuffer>& log_buffer
		) {
			return std::thread([&] {
				while (running.load(std::memory_order_relaxed)) {
					model.set_log_lines(log_buffer->snapshot());
					std::this_thread::sleep_for(config.log_refresh_interval);
				}
			});
		}

		void report_failure(std::string_view header, const mathfp::Error& err) {
			timetable::infra::progress::status(
				header
				, timetable::infra::LogLevel::Error
			);
			timetable::infra::progress::status(
				timetable::app::format_error(err)
				, timetable::infra::LogLevel::Error
			);
		}

		mathfp::Expected<mathfp::Unit> run_worker(
			const io::DataSource& data_source
			, const std::shared_ptr<spdlog::logger>& logger
		) {
			using mathfp::fp::pipe::and_then;
			using mathfp::fp::pipe::inspect;
			using mathfp::fp::pipe::inspect_error;
			using mathfp::fp::pipe::map;
			using mathfp::fp::pipe::map_error;

			return
				data_source.load()
				| inspect_error([logger](const auto& err) {
					if (logger) {
						logger->error(
							"input load failed:\n{}"
							, timetable::app::format_error(err)
						);
					}
				})
				| inspect_error([](const auto& err) {
					report_failure("input load failed", err);
				})
				| map_error([](mathfp::Error err) {
					return std::move(err).ctx(
						"orchestration_stage"
						, std::string("input_load")
					);
				})
				| and_then([&](timetable::domain::AssignmentInput input) {
					return
						timetable::domain::assignment::run_timetable_assignment(std::move(input))
						| inspect_error([logger](const auto& err) {
							if (logger) {
								logger->error(
									"assignment failed:\n{}"
									, timetable::app::format_error(err)
								);
							}
						})
						| inspect_error([](const auto& err) {
							report_failure("assignment failed", err);
						})
						| map_error([](mathfp::Error err) {
							return std::move(err).ctx(
								"orchestration_stage"
								, std::string("assignment")
							);
						})
						| inspect([](const auto&) {
							timetable::infra::progress::status("ready");
						})
						| map([](const auto&) {});
				});
		}

		std::thread start_worker_thread(
			const io::DataSource& data_source
			, const std::shared_ptr<spdlog::logger>& logger
			, WorkerResultBox& worker_result
		) {
			return std::thread([&] {
				(void)(
					worker_result.set(run_worker(data_source, logger))
					| mathfp::fp::pipe::inspect_error([logger](const auto& err) {
						if (logger) {
							logger->error(
								"worker result handoff failed:\n{}",
								timetable::app::format_error(err)
							);
						}
					})
				);
			});
		}

	}  // namespace

	mathfp::Expected<mathfp::Unit> run(
		const AppConfig& config
		, const io::DataSource& data_source
	) {
		MATHFP_TRY_LET(
			timetable::infra::LoggingContext
			, logging
			, timetable::infra::init_logging(
				config.log_capacity
				, config.log_dir
				, config.enable_console_sink
			)
		);
		auto logger = logging.logger;

		ui::UiModel model;
		MATHFP_TRY_LET(ScopedProgressSinks, progress_sinks, ScopedProgressSinks::make(model, logger));
		(void)progress_sinks;
		WorkerResultBox worker_result;

		timetable::infra::progress::status("waiting to start");
		model.set_log_lines(logging.log_buffer->snapshot());

		auto ui_result = [&] {
			BackgroundThreads background_threads(
				model, config, logging.log_buffer, data_source, logger, worker_result
			);
			// TODO: Propagate user-requested UI shutdown into cooperative worker
			// cancellation. Right now pressing q exits the UI loop, but run()
			// still waits for the parsing/assignment worker to finish via join().
			return ui::run(model, logger);
		}();

		MATHFP_TRY(worker_result.take());
		return ui_result;
	}

}  // namespace timetable::app
