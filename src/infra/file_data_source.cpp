#include "timetable/infra/file_data_source.hpp"

#include <array>
#include <filesystem>
#include <utility>

#include <mathfp/core/error.hpp>

namespace timetable::infra {

	namespace {

		class FileDataSource final : public io::DataSource {
		public:
			explicit FileDataSource(io::DataDirSpec spec) : spec_(std::move(spec)) {}

			mathfp::Expected<timetable::domain::AssignmentInput> load() const override {
				return mathfp::unexpected(mathfp::not_implemented("file data source not implemented yet"));
			}

		private:
			io::DataDirSpec spec_;
		};

	}  // namespace

	mathfp::Expected<std::unique_ptr<io::DataSource>> make_file_data_source(
		io::DataDirSpec spec
	) {
		if (spec.root.empty())
			return mathfp::unexpected(mathfp::invalid_arg("data dir path is empty"));

		if (!std::filesystem::exists(spec.root))
			return mathfp::unexpected(
				mathfp::invalid_arg("data dir does not exist")
				.ctx("path", spec.root.string())
			);

		if (!std::filesystem::is_directory(spec.root))
			return mathfp::unexpected(
				mathfp::invalid_arg("data dir is not a directory")
				.ctx("path", spec.root.string())
			);

		static constexpr std::array<const char*, 5> kRequiredFiles = {
			"stops.csv"
			, "trips.csv"
			, "stop_times.csv"
			, "walk_links.csv"
			, "od.csv"
		};
		for (const auto* name : kRequiredFiles) {
			const auto path = spec.root / name;
			if (!std::filesystem::exists(path))
				return mathfp::unexpected(
					mathfp::invalid_arg("missing required input file")
					.ctx("path", path.string())
				);
		}

		const auto params_path = spec.root / "params.json";
		if (!std::filesystem::exists(params_path))
			return mathfp::unexpected(
				mathfp::invalid_arg("missing required params.json")
				.ctx("path", params_path.string())
			);

		return std::make_unique<FileDataSource>(std::move(spec));
	}

}  // namespace timetable::infra
