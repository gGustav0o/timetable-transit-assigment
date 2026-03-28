#pragma once

#include <string_view>

#include <mathfp/core/expected.hpp>

namespace timetable::infra {

    /**
     * @brief Copy UTF-8 text into the platform clipboard.
     */
    mathfp::Expected<mathfp::Unit> copy_text_to_clipboard(std::string_view text);

}  // namespace timetable::infra
