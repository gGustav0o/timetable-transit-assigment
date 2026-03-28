#include "timetable/infra/file_data_source.hpp"

#include <array>
#include <filesystem>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra {

	namespace {

		mathfp::Expected<mathfp::Unit> ensure_existing_data_dir(
			const std::filesystem::path& root
		) {
			if (root.empty())
				return mathfp::unexpected(mathfp::invalid_arg("data dir path is empty"));

			if (!std::filesystem::exists(root))
				return mathfp::unexpected(
					mathfp::invalid_arg("data dir does not exist")
					.ctx("path", root.string())
				);

			if (!std::filesystem::is_directory(root))
				return mathfp::unexpected(
					mathfp::invalid_arg("data dir is not a directory")
					.ctx("path", root.string())
				);

			return mathfp::ok();
		}

		mathfp::Expected<mathfp::Unit> ensure_required_input_files_present(
			const std::filesystem::path& root
		) {
			static constexpr std::array<const char*, 5> kRequiredFiles = {
				"stops.csv"
				, "trips.csv"
				, "stop_times.csv"
				, "walk_links.csv"
				, "od.csv"
			};

			for (const auto* name : kRequiredFiles) {
				const auto path = root / name;
				if (!std::filesystem::exists(path))
					return mathfp::unexpected(
						mathfp::invalid_arg("missing required input file")
						.ctx("path", path.string())
					);
			}

			return mathfp::ok();
		}

		mathfp::Expected<mathfp::Unit> ensure_params_file_present(
			const std::filesystem::path& root
		) {
			const auto params_path = root / "params.json";
			if (!std::filesystem::exists(params_path))
				return mathfp::unexpected(
					mathfp::invalid_arg("missing required params.json")
					.ctx("path", params_path.string())
				);

			return mathfp::ok();
		}

		class FileDataSource final : public io::DataSource {
		public:
			explicit FileDataSource(io::DataDirSpec spec) : spec_(std::move(spec)) {}

			mathfp::Expected<timetable::domain::AssignmentInput> load() const override {
				using timetable::infra::LogLevel;
				using timetable::infra::progress::log;
				using timetable::infra::progress::status;

				status("parsing: deprecated data-dir input selected", LogLevel::Warning);
				log(
					"data-dir input is deprecated, is not maintained against the current program logic, and no further compatibility work is performed on it",
					LogLevel::Warning
				);
				return mathfp::unexpected(mathfp::not_implemented("file data source not implemented yet"));
			}

		private:
			io::DataDirSpec spec_;
		};

	}  // namespace

	mathfp::Expected<std::unique_ptr<io::DataSource>> make_file_data_source(
		io::DataDirSpec spec
	) {
		MATHFP_TRY(ensure_existing_data_dir(spec.root));
		MATHFP_TRY(ensure_required_input_files_present(spec.root));
		MATHFP_TRY(ensure_params_file_present(spec.root));

		return std::make_unique<FileDataSource>(std::move(spec));
	}

}  // namespace timetable::infra
