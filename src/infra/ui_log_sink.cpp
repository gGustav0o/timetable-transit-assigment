#include "timetable/infra/ui_log_sink.hpp"

#include <spdlog/details/log_msg.h>
#include <spdlog/fmt/fmt.h>

namespace timetable::infra {
    namespace {

        LogLevel to_level(spdlog::level::level_enum level) noexcept {
            switch (level) {
                case spdlog::level::debug:
                    return LogLevel::Debug;
                case spdlog::level::info:
                    return LogLevel::Info;
                case spdlog::level::warn:
                    return LogLevel::Warning;
                case spdlog::level::err:
                case spdlog::level::critical:
                    return LogLevel::Error;
                default:
                    return LogLevel::Info;
            }
        }

    }  // namespace

    UiLogSink::UiLogSink(std::shared_ptr<LogBuffer> buffer)
        : buffer_(std::move(buffer)) {
    }

    void UiLogSink::sink_it_(const spdlog::details::log_msg& msg) {
        if (!buffer_) {
            return;
        }
        spdlog::memory_buf_t formatted;
        base_sink<std::mutex>::formatter_->format(msg, formatted);
        buffer_->push(LogEntry{ to_level(msg.level), fmt::to_string(formatted) });
    }

}  // namespace timetable::infra
