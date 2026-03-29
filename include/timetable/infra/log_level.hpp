#pragma once

#include <cstdint>

namespace timetable::infra {

    enum class LogLevel : std::uint8_t {
          Info
        , Warning
        , Error
        , Debug
    };

}  // namespace timetable::infra
