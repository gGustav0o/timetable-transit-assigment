#pragma once

#include <string>

#include "timetable/infra/log_level.hpp"

namespace timetable::infra {

    struct LogEntry {
        LogLevel    level;
        std::string message;
    };

}  // namespace timetable::infra
