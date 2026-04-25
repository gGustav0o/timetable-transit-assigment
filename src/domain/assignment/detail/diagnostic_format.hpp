#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include "timetable/domain/assignment/search_time_domain.hpp"

namespace timetable::domain::assignment::detail::diagnostic {

    [[nodiscard]] inline constexpr std::string_view bool_token(
          bool             value
        , std::string_view true_token
        , std::string_view false_token
    ) noexcept {
        return value ? true_token : false_token;
    }

    [[nodiscard]] inline constexpr std::string_view bool_text(
        bool value
    ) noexcept {
        return bool_token(value, "true", "false");
    }

    [[nodiscard]] inline constexpr std::string_view enabled_text(
        bool value
    ) noexcept {
        return bool_token(value, "on", "off");
    }

    [[nodiscard]] inline std::string format_bounds(
        const SearchTimeWindow& bounds
    ) {
        return fmt::format("[{:.3f}, {:.3f}]", bounds.begin.value(), bounds.end.value());
    }

    [[nodiscard]] inline std::string format_optional_bounds(
          const std::optional<SearchTimeWindow>& bounds
        , std::string_view                       empty_token = "<empty>"
    ) {
        if (!bounds.has_value()) {
            return std::string(empty_token);
        }
        return format_bounds(*bounds);
    }

}  // namespace timetable::domain::assignment::detail::diagnostic
