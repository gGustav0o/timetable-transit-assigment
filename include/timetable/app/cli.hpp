#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/io/io.hpp"

namespace timetable::app {

	struct CliInput {
		io::DataSourceSpec source;
	};

	mathfp::Expected<CliInput> parse_cli(int argc, char** argv);

}  // namespace timetable::app
