#pragma once

#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/runtime_params.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::infra::params_txt::detail {

    struct Value;
    using Object = std::map<std::string, Value>;
    using Array  = std::vector<Value>;

    struct Value final {
        std::variant<std::nullptr_t, bool, double, std::string, Object, Array> data{};
    };

    mathfp::Expected<Value> parse_value_text(
          const std::string& text
        , std::string_view   source_name
    );

    mathfp::Expected<const Object*> as_object(
          const Value&     v
        , std::string_view path
    );

    mathfp::Expected<const Object*> object_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    );

    mathfp::Expected<double> number_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    );

    mathfp::Expected<bool> bool_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    );

    mathfp::Expected<std::string> string_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    );

    mathfp::Expected<timetable::domain::SearchParams> map_params(
        const Object& root
    );

    mathfp::Expected<timetable::domain::AssignmentRuntimeParams> map_assignment_runtime_params(
        const Object& root
    );

}  // namespace timetable::infra::params_txt::detail
