#include "timetable/app/cli.hpp"

#include <filesystem>
#include <string_view>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>

namespace timetable::app {

	namespace {

		constexpr std::string_view kFlagDirLong = "--data-dir";
		constexpr std::string_view kFlagDirShort = "-d";
		constexpr std::string_view kFlagFileLong = "--data-file";
		constexpr std::string_view kFlagFileShort = "-f";

		constexpr std::string_view kDefaultDataFile = "data/txt/CSs_5128.txt";

		std::string usage() {
			return
				"Usage:\n"
				"  timetable-transit-assigment --data-dir <path>\n"
				"  timetable-transit-assigment --data-file <path>\n"
				"Aliases:\n"
				"  -d  --data-dir\n"
				"  -f  --data-file\n";
		}

		mathfp::Expected<std::filesystem::path> find_default_data_file() {
			auto cursor = std::filesystem::current_path();
			while (true) {
				auto candidate = cursor / kDefaultDataFile;
				if (std::filesystem::exists(candidate)) {
					return candidate;
				}
				if (!cursor.has_parent_path() || cursor.parent_path() == cursor) {
					break;
				}
				cursor = cursor.parent_path();
			}
			return mathfp::unexpected(
				mathfp::invalid_arg("default data file not found")
				.ctx("path", std::string(kDefaultDataFile)));
		}

		mathfp::Expected<CliInput> parse_args(int argc, char** argv) {
			CliInput out;
			bool has_dir = false;
			bool has_file = false;

			for (int i = 1; i < argc; ++i) {
				const std::string_view arg(argv[i]);
				const bool is_dir_flag = (arg == kFlagDirLong) || (arg == kFlagDirShort);
				const bool is_file_flag = (arg == kFlagFileLong) || (arg == kFlagFileShort);

				if (!is_dir_flag && !is_file_flag) {
					return mathfp::unexpected(
						mathfp::invalid_arg("unknown command line argument")
						.ctx("arg", std::string(arg))
						.ctx("usage", usage())
					);
				}

				if (i + 1 >= argc) {
					return mathfp::unexpected(
						mathfp::invalid_arg("missing value for command line argument")
						.ctx("arg", std::string(arg))
						.ctx("usage", usage())
					);
				}

				const std::filesystem::path path(argv[i + 1]);
				i += 1;

				if (is_dir_flag) {
					if (has_dir || has_file) {
						return mathfp::unexpected(
							mathfp::invalid_arg("exactly one input source flag is required")
							.ctx("usage", usage())
						);
					}
					out.source.kind = io::DataSourceKind::DataDir;
					out.source.dir.root = path;
					has_dir = true;
					continue;
				}

				if (is_file_flag) {
					if (has_dir || has_file) {
						return mathfp::unexpected(
							mathfp::invalid_arg("exactly one input source flag is required")
							.ctx("usage", usage())
						);
					}
					out.source.kind = io::DataSourceKind::SingleFile;
					out.source.file.path = path;
					has_file = true;
					continue;
				}
			}

			if (!(has_dir ^ has_file)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("exactly one input source flag is required")
					.ctx("usage", usage())
				);
			}

			return out;
		}

	}  // namespace

	mathfp::Expected<CliInput> parse_cli(int argc, char** argv) {
		using mathfp::fp::pipe::map;

		if (argc <= 1) {
			return find_default_data_file()
				| map([](std::filesystem::path path) {
					CliInput out;
					out.source.kind = io::DataSourceKind::SingleFile;
					out.source.file.path = std::move(path);
					return out;
				});
		}

		return parse_args(argc, argv);
	}

}  // namespace timetable::app
