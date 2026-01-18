#include "timetable/app/app.hpp"

#include <filesystem>
#include <memory>

#include <fmt/core.h>
#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/app/error_format.hpp"
#include "timetable/infra/data_sources.hpp"
#include "timetable/infra/logging.hpp"

namespace {

	mathfp::Expected<std::filesystem::path> find_default_data_dir() {
		auto cursor = std::filesystem::current_path();
		while (true) {
			auto candidate = cursor / "data" / "default";
			if (std::filesystem::exists(candidate)) {
				return candidate;
			}
			if (!cursor.has_parent_path() || cursor.parent_path() == cursor) {
				break;
			}
			cursor = cursor.parent_path();
		}
		return mathfp::unexpected(
			mathfp::invalid_arg("default data dir not found"));
	}

	mathfp::Expected<timetable::io::DataSourceSpec> build_data_source_spec(
		int argc
		, char** argv
	) {
		timetable::io::DataSourceSpec spec;
		if (argc > 1) {
			spec.file.root = std::filesystem::path(argv[1]);
			return spec;
		}
#ifndef NDEBUG
		return find_default_data_dir() |
			mathfp::fp::pipe::map([&](std::filesystem::path path) {
			spec.file.root = std::move(path);
			return spec;
				});
#else
		return mathfp::unexpected(
			mathfp::invalid_arg("missing required data_dir argument"));
#endif
	}

	mathfp::Expected<mathfp::Unit> run_app(int argc, char** argv) {
		timetable::app::AppConfig config;
		return build_data_source_spec(argc, argv) |
			mathfp::fp::pipe::and_then(
				[&](const timetable::io::DataSourceSpec& spec)
				-> mathfp::Expected<mathfp::Unit> {
					return timetable::infra::make_data_source(spec) |
						mathfp::fp::pipe::and_then(
							[&](std::unique_ptr<timetable::io::DataSource> ds)
							-> mathfp::Expected<mathfp::Unit> {
								return timetable::app::run(config, *ds);
							});
				});
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
