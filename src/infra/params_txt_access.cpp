#include "detail/params_txt.hpp"

#include <string>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::infra::params_txt::detail {

    namespace {

        mathfp::Expected<const Value*> object_get(
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
            return &it->second;
        }

    }  // namespace

    mathfp::Expected<const Object*> as_object(
          const Value&     v
        , std::string_view path
    ) {
        if (!std::holds_alternative<Object>(v.data)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("expected object")
                .ctx("path", std::string(path))
            );
        }
        return &std::get<Object>(v.data);
    }

    mathfp::Expected<const Object*> object_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        MATHFP_TRY_LET(const Value*, child, object_get(obj, key, path));
        const auto child_path = std::string(path) + "." + std::string(key);
        return as_object(*child, child_path);
    }

    mathfp::Expected<double> number_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        MATHFP_TRY_LET(const Value*, child, object_get(obj, key, path));
        if (!std::holds_alternative<double>(child->data)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("expected number")
                .ctx("path", std::string(path))
                .ctx("key" , std::string(key))
            );
        }
        return std::get<double>(child->data);
    }

    mathfp::Expected<std::string> string_at(
          const Object&    obj
        , std::string_view key
        , std::string_view path
    ) {
        MATHFP_TRY_LET(const Value*, child, object_get(obj, key, path));
        if (!std::holds_alternative<std::string>(child->data)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("expected string")
                .ctx("path", std::string(path))
                .ctx("key" , std::string(key))
            );
        }
        return std::get<std::string>(child->data);
    }

}  // namespace timetable::infra::params_txt::detail
