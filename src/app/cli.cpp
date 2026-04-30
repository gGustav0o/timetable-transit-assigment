#include "timetable/app/cli.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::app {

    namespace {
        enum class CliSourceFlag : std::uint8_t {
              DataDir
            , PairDataDir
            , DataFile
        };

        constexpr std::string_view kFlagDirLong      = "--data-dir";
        constexpr std::string_view kFlagDirShort     = "-d";
        constexpr std::string_view kFlagPairDirLong  = "--pair-data-dir";
        constexpr std::string_view kFlagPairDirShort = "-p";
        constexpr std::string_view kFlagFileLong     = "--data-file";
        constexpr std::string_view kFlagFileShort    = "-f";

        constexpr std::string_view kDefaultPairDataDir = "data/test";

        std::string usage() {
            return "Usage:\n"
                "  timetable-transit-assigment --pair-data-dir <path> [supported runtime path]\n"
                "  timetable-transit-assigment --data-dir <path>      [deprecated compatibility path]\n"
                "  timetable-transit-assigment --data-file <path>     [deprecated compatibility path]\n"
                "Default:\n"
                "  without arguments the app searches upward for data/test/connection_segments_input.csv\n"
                "Current scope:\n"
                "  pair-file input only; params.txt supplies the supported SearchParams subset when present\n"
                "Aliases:\n"
                "  -p  --pair-data-dir [supported]\n"
                "  -d  --data-dir      [deprecated]\n"
                "  -f  --data-file     [deprecated]\n";
        }

        mathfp::Unexpected make_single_source_error() {
            return mathfp::unexpected(
                mathfp::invalid_arg("exactly one input source flag is required")
                .ctx("usage", usage())
            );
        }

        mathfp::Expected<CliSourceFlag> classify_source_flag(std::string_view arg) {
            if (arg == kFlagDirLong || arg == kFlagDirShort) {
                return CliSourceFlag::DataDir;
            }
            if (arg == kFlagPairDirLong || arg == kFlagPairDirShort) {
                return CliSourceFlag::PairDataDir;
            }
            if (arg == kFlagFileLong || arg == kFlagFileShort) {
                return CliSourceFlag::DataFile;
            }
            return mathfp::unexpected(
                mathfp::invalid_arg("unknown command line argument")
                .ctx("arg"  , std::string(arg))
                .ctx("usage", usage())
            );
        }

        mathfp::Expected<std::filesystem::path> parse_flag_value(
              int              argc
            , char**           argv
            , int              flag_index
            , std::string_view flag
        ) {
            if (flag_index + 1 >= argc) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("missing value for command line argument")
                    .ctx("arg"  , std::string(flag))
                    .ctx("usage", usage())
                );
            }
            return std::filesystem::path(argv[flag_index + 1]);
        }

        CliInput make_cli_input(
              CliSourceFlag         flag
            , std::filesystem::path path
        ) {
            CliInput out;
            switch (flag) {
                case CliSourceFlag::DataDir:
                    out.source.kind     = io::DataSourceKind::DataDir;
                    out.source.dir.root = std::move(path);
                    break;
                case CliSourceFlag::PairDataDir:
                    out.source.kind          = io::DataSourceKind::PairDataDir;
                    out.source.pair_dir.root = std::move(path);
                    break;
                case CliSourceFlag::DataFile:
                    out.source.kind      = io::DataSourceKind::SingleFile;
                    out.source.file.path = std::move(path);
                    break;
            }
            return out;
        }

        CliInput make_pair_cli_input(std::filesystem::path path) {
            return make_cli_input(CliSourceFlag::PairDataDir, std::move(path));
        }

        mathfp::Expected<mathfp::Unit> ensure_no_source_selected(bool has_source) {
            if (has_source) {
                return make_single_source_error();
            }
            return mathfp::ok();
        }

        mathfp::Expected<mathfp::Unit> apply_selected_source(
              CliInput& out
            , bool&     has_source
            , CliInput  selected
        ) {
            MATHFP_TRY(ensure_no_source_selected(has_source));
            out        = std::move(selected);
            has_source = true;
            return mathfp::ok();
        }

        mathfp::Expected<mathfp::Unit> ensure_single_source_selected(bool has_source) {
            if (!has_source) {
                return make_single_source_error();
            }
            return mathfp::ok();
        }

        bool is_pair_data_dir(const std::filesystem::path& path) {
            const auto segments = path / "connection_segments_input.csv";
            return std::filesystem::exists(path)
                && std::filesystem::is_directory(path)
                && std::filesystem::exists(segments);
        }

        mathfp::Expected<std::filesystem::path> find_default_pair_data_dir() {
            auto cursor = std::filesystem::current_path();
            while (true) {
                auto candidate = cursor / kDefaultPairDataDir;
                if (is_pair_data_dir(candidate)) {
                    return candidate;
                }
                if (!cursor.has_parent_path() || cursor.parent_path() == cursor) {
                    break;
                }
                cursor = cursor.parent_path();
            }
            return mathfp::unexpected(
                mathfp::invalid_arg("default pair data dir not found")
                .ctx("path"    , std::string(kDefaultPairDataDir))
                .ctx("required", "connection_segments_input.csv"));
        }

        mathfp::Expected<CliInput> parse_args(int argc, char** argv) {
            CliInput out;
            bool has_source = false;

            for (int i = 1; i < argc; ++i) {
                const std::string_view arg(argv[i]);
                const auto flag = classify_source_flag(arg);
                if (!flag) {
                    return mathfp::unexpected(flag.error());
                }

                auto path = parse_flag_value(argc, argv, i, arg);
                if (!path) {
                    return mathfp::unexpected(path.error());
                }

                i += 1;

                auto selected = make_cli_input(
                      *flag
                    , std::move(*path)
                );

                auto applied = apply_selected_source(
                      out
                    , has_source
                    , std::move(selected)
                );
                if (!applied) {
                    return mathfp::unexpected(applied.error());
                }
            }

            auto ensured = ensure_single_source_selected(has_source);
            if (!ensured) {
                return mathfp::unexpected(ensured.error());
            }

            return out;
        }

    }  // namespace

    mathfp::Expected<CliInput> parse_cli(int argc, char** argv) {
        using mathfp::fp::pipe::map;

        if (argc <= 1) {
            return find_default_pair_data_dir()
                | map(make_pair_cli_input);
        }

        return parse_args(argc, argv);
    }

}  // namespace timetable::app
