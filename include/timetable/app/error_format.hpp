#pragma once

#include <string>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>

namespace timetable::app {

    inline std::string format_error(const mathfp::Error& err) {
        const auto& where = err.where();
        return fmt::format(
              "{}: {}\n  at {}:{} in {}\n  ctx: {}"
            , err.kind()
            , err.message()
            , where.file_name()
            , where.line()
            , where.function_name()
            , err.context().to_string()
        );
    }

}  // namespace timetable::app
