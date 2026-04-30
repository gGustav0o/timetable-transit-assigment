#pragma once

#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/io/io.hpp"

namespace timetable::infra {

    /**
     * @brief Primary maintained data source.
     *
     * This pair-file path is the actively supported input contract for the current
     * program logic and ongoing architectural changes.
     *
     * Current scope note:
     * - this is the only supported runtime path for the minimal working project;
     * - params.txt is used for modeled runtime parameters;
     * - runtime rollout configs still use built-in defaults.
     */
    mathfp::Expected<std::unique_ptr<io::DataSource>> make_pair_file_data_source(
        io::PairDataDirSpec spec
    );

}  // namespace timetable::infra
