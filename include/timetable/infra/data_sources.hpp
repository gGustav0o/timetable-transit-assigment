#pragma once

#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/io/io.hpp"

namespace timetable::infra {

    mathfp::Expected<std::unique_ptr<io::DataSource>> make_data_source(
        const io::DataSourceSpec& spec
    );

}  // namespace timetable::infra
