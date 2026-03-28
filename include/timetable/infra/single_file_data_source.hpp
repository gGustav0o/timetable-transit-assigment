#pragma once

#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/io/io.hpp"

namespace timetable::infra {

	/**
	 * @brief Deprecated compatibility data source.
	 *
	 * This single-file TXT path is intentionally not maintained together with the
	 * actively evolving program logic. Pair-file input is the current source of truth.
	 */
	mathfp::Expected<std::unique_ptr<io::DataSource>> make_single_file_data_source(
		io::DataFileSpec spec
	);

}  // namespace timetable::infra
