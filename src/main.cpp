#include "timetable/app/app.hpp"
#include "timetable/app/cli.hpp"

#include <memory>

#include <fmt/core.h>
#include <mathfp/core/fp.hpp>

#include "timetable/app/error_format.hpp"
#include "timetable/infra/data_sources.hpp"
#include "timetable/infra/logging.hpp"

namespace {

	mathfp::Expected<mathfp::Unit> run_app(int argc, char** argv) {
		using mathfp::fp::pipe::and_then;
		using mathfp::fp::pipe::map;

		timetable::app::AppConfig config;

		auto run_with_data_source = [&](std::unique_ptr<timetable::io::DataSource> ds) {
			return timetable::app::run(config, *ds);
		};

		return
			timetable::app::parse_cli(argc, argv)
			| map([](timetable::app::CliInput cli) { return cli.source; })
			| and_then(timetable::infra::make_data_source)
			| and_then(run_with_data_source);
	}

}  // namespace

int main(int argc, char** argv) {
	auto result = run_app(argc, argv);
	if (!result) {
		if (!timetable::infra::logging_started()) {
			fmt::print(stderr, "{}\n", timetable::app::format_error(result.error()));
		}
		return 1;
	}
	return 0;
}
