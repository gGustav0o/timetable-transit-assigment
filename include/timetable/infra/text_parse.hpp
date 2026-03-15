#pragma once

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace timetable::infra::text_parse {

    enum class NumericParseFailure : std::uint8_t {
        Empty
        , Invalid
        , Range
    };

    [[nodiscard]] inline std::string_view trim_ascii(
        std::string_view text
    ) {
        std::size_t begin = 0;
        std::size_t end = text.size();

        while (begin < end
            && std::isspace(static_cast<unsigned char>(text[begin]))) {
            ++begin;
        }

        while (end > begin
            && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
            --end;
        }

        return text.substr(begin, end - begin);
    }

    [[nodiscard]] inline std::string_view trim_trailing_cr(
        std::string_view text
    ) noexcept {
        if (!text.empty() && text.back() == '\r') {
            text.remove_suffix(1);
        }
        return text;
    }

    template <typename T, typename ParseFn>
    [[nodiscard]] std::variant<std::pair<T, const char*>, NumericParseFailure>
    parse_numeric_prefix(
        const char* begin
        , ParseFn&& parse
    ) {
        errno = 0;
        char* end = nullptr;
        const auto value = parse(begin, &end);

        if (end == begin) {
            return NumericParseFailure::Invalid;
        }

        if (errno == ERANGE) {
            return NumericParseFailure::Range;
        }

        return std::pair<T, const char*>{ static_cast<T>(value), end };
    }

    template <typename T, typename ParseFn>
    [[nodiscard]] std::variant<T, NumericParseFailure> parse_numeric_token(
        std::string_view text
        , ParseFn&& parse
    ) {
        const auto trimmed = trim_ascii(text);
        if (trimmed.empty()) {
            return NumericParseFailure::Empty;
        }

        const auto token = std::string(trimmed);
        const auto result = parse_numeric_prefix<T>(token.c_str(), std::forward<ParseFn>(parse));
        if (const auto* failure = std::get_if<NumericParseFailure>(&result)) {
            return *failure;
        }

        const auto& [value, end] = std::get<std::pair<T, const char*>>(result);
        if (*end != '\0') {
            return NumericParseFailure::Invalid;
        }

        return value;
    }

}  // namespace timetable::infra::text_parse
