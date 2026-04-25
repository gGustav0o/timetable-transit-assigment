#pragma once

#include <array>
#include <optional>
#include <string_view>
#include <type_traits>

namespace timetable {

    template <class Enum>
    struct EnumStringEntry final {
        Enum             value{};
        std::string_view token{};
    };

    template <class Enum, std::size_t N>
    [[nodiscard]] constexpr std::string_view enum_to_string(
          Enum                                        value
        , const std::array<EnumStringEntry<Enum>, N>& entries
        , std::string_view                            fallback = "<unknown>"
    ) noexcept {
        static_assert(std::is_enum_v<Enum>);

        for (const auto& entry : entries) {
            if (entry.value == value) {
                return entry.token;
            }
        }
        return fallback;
    }

    template <class Enum, std::size_t N>
    [[nodiscard]] constexpr std::optional<Enum> enum_from_string(
          std::string_view                            token
        , const std::array<EnumStringEntry<Enum>, N>& entries
    ) noexcept {
        static_assert(std::is_enum_v<Enum>);

        for (const auto& entry : entries) {
            if (entry.token == token) {
                return entry.value;
            }
        }
        return std::nullopt;
    }

}  // namespace timetable
