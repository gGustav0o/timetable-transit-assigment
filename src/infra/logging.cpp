#include "timetable/infra/logging.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <ctime>
#include <mutex>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <fmt/format.h>

#include "timetable/infra/ui_log_sink.hpp"

namespace timetable::infra {
    namespace {
        // TODO: Formalize the process-wide logging lifecycle. The current flag is
        // monotone (false -> true) and intentionally never resets, but that policy
        // should be made explicit in the subsystem contract rather than remaining
        // an implicit helper for main.cpp.
        std::atomic_bool g_logging_started        = false;
        std::atomic_bool g_periodic_flush_started = false;

        std::mutex g_logging_mutex;

        spdlog::level::level_enum to_spdlog_level(
            LogLevel level
        ) noexcept {
            switch (level) {
                case LogLevel::Debug:
                    return spdlog::level::debug;
                case LogLevel::Info:
                    return spdlog::level::info;
                case LogLevel::Warning:
                    return spdlog::level::warn;
                case LogLevel::Error:
                    return spdlog::level::err;
            }
            return spdlog::level::err;
        }

        mathfp::Expected<mathfp::Unit> ensure_log_directory_ready(
            const std::filesystem::path& log_dir
        ) {
            std::error_code ec;
            std::filesystem::create_directories(log_dir, ec);
            if (ec) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed to create log directory")
                    .ctx("path"      , log_dir.string())
                    .ctx("error_code", static_cast<std::int64_t>(ec.value()))
                    .ctx("message"   , ec.message())
                );
            }

            if (!std::filesystem::exists(log_dir)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("log directory does not exist after creation")
                    .ctx("path", log_dir.string())
                );
            }

            if (!std::filesystem::is_directory(log_dir)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("log path is not a directory")
                    .ctx("path", log_dir.string())
                );
            }

            return mathfp::ok();
        }

        std::filesystem::path make_log_file_path(
            const std::filesystem::path& log_dir
        ) {
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
            return log_dir / log_name;
        }

        std::vector<spdlog::sink_ptr> make_logging_sinks(
              const std::filesystem::path&      log_path
            , const std::shared_ptr<LogBuffer>& log_buffer
            , bool                              enable_console_sink
        ) {
            auto file_sink
                = std::make_shared<spdlog::sinks::basic_file_sink_mt>(log_path.string(), true);
            auto ui_sink = std::make_shared<UiLogSink>(log_buffer);
            std::vector<spdlog::sink_ptr> sinks;
            sinks.reserve(enable_console_sink ? 3u : 2u);
            sinks.push_back(file_sink);
            sinks.push_back(ui_sink);
            if (enable_console_sink) {
                sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
            }
            return sinks;
        }

        std::shared_ptr<spdlog::logger> make_logger(
            const std::vector<spdlog::sink_ptr>& sinks
        ) {
            return std::make_shared<spdlog::logger>(
                "timetable", sinks.begin(), sinks.end()
            );
        }

        mathfp::Expected<mathfp::Unit> activate_logging(
              const std::shared_ptr<spdlog::logger>& logger
            , const LoggingPolicy&                   policy
        ) {
            if (!logger) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("cannot activate logging with null logger")
                );
            }

            logger->flush_on(to_spdlog_level(policy.flush_on_level));
            // TODO: Define repeated init_logging(...) semantics explicitly:
            // whether replacing the default logger is allowed, expected, or should
            // be rejected/reused for process-wide stability.
            {
                std::lock_guard lock(g_logging_mutex);
                spdlog::set_default_logger(logger);
                spdlog::set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
            }

            if (policy.periodic_flush_interval.count() > 0
                && !g_periodic_flush_started.exchange(true, std::memory_order_relaxed)) {
                spdlog::flush_every(policy.periodic_flush_interval);
            }

            g_logging_started.store(true, std::memory_order_relaxed);
            return mathfp::ok();
        }

        struct LoggingArtifacts final {
            LoggingContext        context{};
            std::filesystem::path log_path{};
        };

        mathfp::Expected<LoggingArtifacts> make_logging_artifacts(
              std::size_t                  log_capacity
            , const std::filesystem::path& log_dir
            , bool                         enable_console_sink
        ) {
            MATHFP_TRY(ensure_log_directory_ready(log_dir));
            auto log_buffer     = std::make_shared<LogBuffer>(log_capacity);
            const auto log_path = make_log_file_path(log_dir);
            const auto sinks    = make_logging_sinks(log_path, log_buffer, enable_console_sink);
            auto logger         = make_logger(sinks);
            return LoggingArtifacts{
                  .context  = LoggingContext{ std::move(logger), std::move(log_buffer) }
                , .log_path = std::move(log_path)
            };
        }
    }

    mathfp::Expected<LoggingContext> init_logging(
          std::size_t                  log_capacity
        , const std::filesystem::path& log_dir
        , bool                         enable_console_sink
        , LoggingPolicy                policy
    ) {
        MATHFP_TRY_LET(
              LoggingArtifacts
            , artifacts
            , make_logging_artifacts(
            log_capacity, log_dir, enable_console_sink
        ));
        MATHFP_TRY(activate_logging(artifacts.context.logger, policy));
        artifacts.context.logger->info(
              "log file: {}"
            , artifacts.log_path.string()
        );
        return std::move(artifacts.context);
    }

    mathfp::Expected<mathfp::Unit> flush_logging() {
        if (!logging_started()) {
            return mathfp::ok();
        }

        std::lock_guard lock(g_logging_mutex);
        const auto logger = spdlog::default_logger();
        if (!logger) {
            return mathfp::unexpected(
                mathfp::internal_error("logging is marked active but default logger is missing")
            );
        }

        logger->flush();
        return mathfp::ok();
    }

    bool logging_started() noexcept {
        return g_logging_started.load(std::memory_order_relaxed);
    }

}  // namespace timetable::infra
