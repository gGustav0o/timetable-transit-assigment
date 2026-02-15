#pragma once

#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/io/io.hpp"

namespace timetable::infra {

	mathfp::Expected<std::unique_ptr<io::DataSource>> make_pair_file_data_source(
		io::PairDataDirSpec spec
	);

}  // namespace timetable::infra

