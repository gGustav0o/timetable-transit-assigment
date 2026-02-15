#include "timetable/app/cli.hpp"

#include <filesystem>
#include <string_view>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>

namespace timetable::app {

	namespace {

		constexpr std::string_view kFlagDirLong = "--data-dir";
		constexpr std::string_view kFlagDirShort = "-d";
		constexpr std::string_view kFlagPairDirLong = "--pair-data-dir";
		constexpr std::string_view kFlagPairDirShort = "-p";
		constexpr std::string_view kFlagFileLong = "--data-file";
		constexpr std::string_view kFlagFileShort = "-f";

		constexpr std::string_view kDefaultPairDataDir = "data/test";

		std::string usage() {
			return
				"Usage:\n"
				"  timetable-transit-assigment --data-dir <path>\n"
				"  timetable-transit-assigment --pair-data-dir <path>\n"
				"  timetable-transit-assigment --data-file <path>\n"
				"Aliases:\n"
				"  -d  --data-dir\n"
				"  -p  --pair-data-dir\n"
				"  -f  --data-file\n";
		}

		mathfp::Expected<std::filesystem::path> find_default_pair_data_dir() {
			auto cursor = std::filesystem::current_path();
			while (true) {
				auto candidate = cursor / kDefaultPairDataDir;
				const auto segments = candidate / "connection_segments_input.csv";
				const auto params = candidate / "params.txt";
				if (
					std::filesystem::exists(candidate)
					&& std::filesystem::is_directory(candidate)
					&& std::filesystem::exists(segments)
					&& std::filesystem::exists(params)
				) {
					return candidate;
				}
				if (!cursor.has_parent_path() || cursor.parent_path() == cursor) {
					break;
				}
				cursor = cursor.parent_path();
			}
			return mathfp::unexpected(
				mathfp::invalid_arg("default pair data dir not found")
				.ctx("path", std::string(kDefaultPairDataDir))
				.ctx("required", "connection_segments_input.csv + params.txt"));
		}

		mathfp::Expected<CliInput> parse_args(int argc, char** argv) {
			CliInput out;
			bool has_dir = false;
			bool has_pair_dir = false;
			bool has_file = false;

			for (int i = 1; i < argc; ++i) {
				const std::string_view arg(argv[i]);
				const bool is_dir_flag = (arg == kFlagDirLong) || (arg == kFlagDirShort);
				const bool is_pair_dir_flag = (arg == kFlagPairDirLong) || (arg == kFlagPairDirShort);
				const bool is_file_flag = (arg == kFlagFileLong) || (arg == kFlagFileShort);

				if (!is_dir_flag && !is_pair_dir_flag && !is_file_flag) {
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
					if (has_dir || has_pair_dir || has_file) {
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

				if (is_pair_dir_flag) {
					if (has_dir || has_pair_dir || has_file) {
						return mathfp::unexpected(
							mathfp::invalid_arg("exactly one input source flag is required")
							.ctx("usage", usage())
						);
					}
					out.source.kind = io::DataSourceKind::PairDataDir;
					out.source.pair_dir.root = path;
					has_pair_dir = true;
					continue;
				}

				if (is_file_flag) {
					if (has_dir || has_pair_dir || has_file) {
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

			if ((has_dir ? 1 : 0) + (has_pair_dir ? 1 : 0) + (has_file ? 1 : 0) != 1) {
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
			return find_default_pair_data_dir()
				| map([](std::filesystem::path path) {
					CliInput out;
					out.source.kind = io::DataSourceKind::PairDataDir;
					out.source.pair_dir.root = std::move(path);
					return out;
				});
		}

		return parse_args(argc, argv);
	}

}  // namespace timetable::app
