#include "timetable/infra/progress_bus.hpp"

#include <mutex>
#include <string_view>
#include <utility>

namespace timetable::infra::progress {
    namespace {

        Sink make_noop_sink() {
            return [](LogLevel, std::string_view) {};
        }

        Sink normalize_sink(Sink sink) {
            return sink ? std::move(sink) : make_noop_sink();
        }

        SinkState normalize_sinks(SinkState sinks) {
            sinks.status = normalize_sink(std::move(sinks.status));
            sinks.log    = normalize_sink(std::move(sinks.log));
            return sinks;
        }

        class ProgressBusState final {
        public:
            mathfp::Expected<mathfp::Unit> set_sinks(SinkState sinks) {
                std::lock_guard lock(mutex_);
                sinks_ = normalize_sinks(std::move(sinks));
                return mathfp::ok();
            }

            mathfp::Expected<mathfp::Unit> set_status_sink(Sink sink) {
                std::lock_guard lock(mutex_);
                sinks_.status = normalize_sink(std::move(sink));
                return mathfp::ok();
            }

            mathfp::Expected<mathfp::Unit> set_log_sink(Sink sink) {
                std::lock_guard lock(mutex_);
                sinks_.log = normalize_sink(std::move(sink));
                return mathfp::ok();
            }

            void status(std::string_view message, LogLevel level) {
                copy_status_sink()(level, message);
            }

            void log(std::string_view message, LogLevel level) {
                copy_log_sink()(level, message);
            }

        private:
            Sink copy_status_sink() {
                std::lock_guard lock(mutex_);
                return sinks_.status;
            }

            Sink copy_log_sink() {
                std::lock_guard lock(mutex_);
                return sinks_.log;
            }

            std::mutex mutex_{};
            SinkState sinks_ = normalize_sinks(SinkState{});
        };

        ProgressBusState& bus() {
            static ProgressBusState state;
            return state;
        }
    }

    mathfp::Expected<mathfp::Unit> set_sinks(SinkState sinks) {
        return bus().set_sinks(std::move(sinks));
    }

    mathfp::Expected<mathfp::Unit> set_status_sink(Sink sink) {
        return bus().set_status_sink(std::move(sink));
    }

    mathfp::Expected<mathfp::Unit> set_log_sink(Sink sink) {
        return bus().set_log_sink(std::move(sink));
    }

    mathfp::Expected<mathfp::Unit> clear_sinks() {
        return set_sinks(SinkState{});
    }

    void status(std::string_view message, LogLevel level) {
        bus().status(message, level);
    }

    void log(std::string_view message, LogLevel level) {
        bus().log(message, level);
    }

    void both(std::string_view message, LogLevel level) {
        status(message, level);
        log(message, level);
    }

}  // namespace timetable::infra::progress
