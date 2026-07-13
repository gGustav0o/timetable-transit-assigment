#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>

#include "params_txt.hpp"

namespace timetable::infra::params_txt::detail::codecs {

    enum class TemporalParameterUnit : std::uint8_t {
          Minutes
        , Seconds
    };

    inline constexpr TemporalParameterUnit kParamsTxtTemporalParameterUnit =
        TemporalParameterUnit::Minutes;

    [[nodiscard]] constexpr double temporal_parameter_unit_multiplier(
        TemporalParameterUnit unit
    ) noexcept {
        switch (unit) {
            case TemporalParameterUnit::Minutes:
                return 60.0;
            case TemporalParameterUnit::Seconds:
                return 1.0;
        }
        return 1.0;
    }

    [[nodiscard]] constexpr double temporal_parameter_seconds(
          double                raw_value
        , TemporalParameterUnit unit = kParamsTxtTemporalParameterUnit
    ) noexcept {
        return raw_value * temporal_parameter_unit_multiplier(unit);
    }

    inline mathfp::Expected<bool> numeric_bool(
          double           value
        , std::string_view field_name
    ) {
        if (value == 0.0) {
            return false;
        }
        if (value == 1.0) {
            return true;
        }
        return mathfp::unexpected(
            mathfp::invalid_arg("expected numeric bool encoded as 0 or 1")
                .ctx("field", std::string(field_name))
                .ctx("value", value)
        );
    }

    inline mathfp::Expected<bool> bool_like_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        const auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("missing required key")
                    .ctx("path", std::string(path))
                    .ctx("key" , std::string(key))
            );
        }
        if (const auto* value = std::get_if<bool>(&it->second.data)) {
            return *value;
        }
        if (const auto* value = std::get_if<double>(&it->second.data)) {
            return numeric_bool(
                  *value
                , std::string(path) + "." + std::string(key)
            );
        }
        return mathfp::unexpected(
            mathfp::invalid_arg("expected bool or numeric bool encoded as 0 or 1")
                .ctx("path", std::string(path))
                .ctx("key" , std::string(key))
        );
    }

    inline mathfp::Expected<std::optional<bool>> optional_bool_like_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        const auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return std::nullopt;
        }
        if (const auto* value = std::get_if<bool>(&it->second.data)) {
            return *value;
        }
        if (const auto* value = std::get_if<double>(&it->second.data)) {
            MATHFP_TRY_LET(
                  bool
                , parsed
                , numeric_bool(
                      *value
                    , std::string(path) + "." + std::string(key)
                  )
            );
            return parsed;
        }
        return mathfp::unexpected(
            mathfp::invalid_arg("expected optional bool or numeric bool encoded as 0 or 1")
                .ctx("path", std::string(path))
                .ctx("key" , std::string(key))
        );
    }

    inline mathfp::Expected<std::optional<double>> optional_number_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        const auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return std::nullopt;
        }
        if (const auto* value = std::get_if<double>(&it->second.data)) {
            return *value;
        }
        return mathfp::unexpected(
            mathfp::invalid_arg("expected optional number")
                .ctx("path", std::string(path))
                .ctx("key" , std::string(key))
        );
    }

    inline mathfp::Expected<std::optional<std::size_t>> optional_positive_size_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        const auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return std::nullopt;
        }
        const auto* value = std::get_if<double>(&it->second.data);
        if (value == nullptr
            || !std::isfinite(*value)
            || *value < 1.0
            || *value > static_cast<double>(std::numeric_limits<std::size_t>::max())
            || std::trunc(*value) != *value) {
            return mathfp::unexpected(
                mathfp::invalid_arg("expected optional positive integer")
                    .ctx("path", std::string(path))
                    .ctx("key" , std::string(key))
            );
        }
        return static_cast<std::size_t>(*value);
    }

    inline mathfp::Expected<const Object*> optional_object_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        const auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return static_cast<const Object*>(nullptr);
        }
        const auto* child = std::get_if<Object>(&it->second.data);
        if (child == nullptr) {
            return mathfp::unexpected(
                mathfp::invalid_arg("expected optional object")
                    .ctx("path", std::string(path))
                    .ctx("key" , std::string(key))
            );
        }
        return child;
    }

    inline mathfp::Expected<std::optional<std::string>> optional_string_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        const auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            return std::nullopt;
        }
        if (const auto* str = std::get_if<std::string>(&it->second.data)) {
            return *str;
        }
        return mathfp::unexpected(
            mathfp::invalid_arg("expected optional string")
                .ctx("path", std::string(path))
                .ctx("key" , std::string(key))
        );
    }

}  // namespace timetable::infra::params_txt::detail::codecs
