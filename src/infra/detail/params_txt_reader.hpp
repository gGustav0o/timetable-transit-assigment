#pragma once

#include <array>
#include <string>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>

#include "params_txt_schema.hpp"

namespace timetable::infra::params_txt::detail::reader {

    template <class Draft, class Value, std::size_t N, class Read>
    mathfp::Expected<Draft> read_draft(
          const Object&                                     obj
        , const std::array<schema::FieldSpec<Draft, Value>, N>& specs
        , Read&&                                            read
    ) {
        Draft out{};
        for (const auto& spec : specs) {
            auto value = read(obj, spec);
            if (!value) {
                return mathfp::unexpected(std::move(value.error()));
            }
            out.*(spec.member) = std::move(*value);
        }
        return out;
    }

    template <class Draft, std::size_t N>
    mathfp::Expected<Draft> read_object_draft(
          const Object&                                         obj
        , const std::array<schema::ObjectFieldSpec<Draft>, N>& specs
    ) {
        return read_draft<Draft, const Object*>(obj, specs, [](const Object& source, const auto& spec) {
            return object_at(source, spec.key, spec.path);
        });
    }

    template <class Draft, std::size_t N>
    mathfp::Expected<Draft> read_number_draft(
          const Object&                                         obj
        , const std::array<schema::NumberFieldSpec<Draft>, N>& specs
    ) {
        return read_draft<Draft, double>(obj, specs, [](const Object& source, const auto& spec) {
            return number_at(source, spec.key, spec.path);
        });
    }

    template <class Draft, std::size_t N>
    mathfp::Expected<Draft> read_string_draft(
          const Object&                                         obj
        , const std::array<schema::StringFieldSpec<Draft>, N>& specs
    ) {
        return read_draft<Draft, std::string>(obj, specs, [](const Object& source, const auto& spec) {
            return string_at(source, spec.key, spec.path);
        });
    }

}  // namespace timetable::infra::params_txt::detail::reader
