#pragma once

#include <functional>
#include <string_view>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/infra/log_level.hpp"

namespace timetable::infra::progress {

    using Sink = std::function<void(LogLevel, std::string_view)>;

    struct SinkState final {
        Sink status{};
        Sink log{};
    };

    // Process-wide progress diagnostics bus. When no sinks are installed, it
    // degrades to no-op delivery rather than failing.
    //
    // TODO:
    // Domain algorithms currently report progress through this global infra
    // bus. This is an intentional short-term compromise: it keeps the runtime
    // surface small and avoids threading a diagnostic context through every
    // search, choice, split and preprocessing routine.
    //
    // The clean design would keep domain computations pure:
    // algorithms would return their result together with structured diagnostic
    // values, or accumulate those values through a writer-style monoid. The
    // application/infra layer would then interpret those values as logs,
    // status updates or UI messages.
    //
    // A pragmatic intermediate design is an explicit domain-owned callback
    // interface, passed as part of a run/search context. That still represents
    // an effect, but it is no longer hidden global state and domain would not
    // depend on infra logging. After this dependency is removed, the build
    // should be split into real layers such as timetable_domain,
    // timetable_infra, timetable_ui and timetable_app, with logging, CSV,
    // xlsxio and FTXUI kept private to the layers that use them.
    mathfp::Expected<mathfp::Unit> set_sinks(SinkState sinks);
    mathfp::Expected<mathfp::Unit> set_status_sink(Sink sink);
    mathfp::Expected<mathfp::Unit> set_log_sink(Sink sink);
    mathfp::Expected<mathfp::Unit> clear_sinks();

    void status(std::string_view message, LogLevel level = LogLevel::Info);
    void log(std::string_view message, LogLevel level    = LogLevel::Info);
    void both(std::string_view message, LogLevel level   = LogLevel::Info);

}  // namespace timetable::infra::progress
