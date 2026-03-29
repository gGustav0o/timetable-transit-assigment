#pragma once

#include <chrono>
#include <filesystem>
#include <memory>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/infra/log_buffer.hpp"
#include "timetable/infra/log_level.hpp"

namespace spdlog {
    class logger;
}  // namespace spdlog

namespace timetable::infra {

    struct LoggingPolicy final {
        LogLevel                  flush_on_level = LogLevel::Error;
        std::chrono::milliseconds periodic_flush_interval{ 500 };
    };

    struct LoggingContext {
        std::shared_ptr<spdlog::logger> logger;
        std::shared_ptr<LogBuffer>      log_buffer;
    };

    mathfp::Expected<LoggingContext> init_logging(
          std::size_t                  log_capacity
        , const std::filesystem::path& log_dir
        , bool                         enable_console_sink = true
        , LoggingPolicy                policy = {}
    );

    mathfp::Expected<mathfp::Unit> flush_logging();

    bool logging_started() noexcept;

}  // namespace timetable::infra
