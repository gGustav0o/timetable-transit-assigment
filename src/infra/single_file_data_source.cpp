#include "timetable/infra/single_file_data_source.hpp"
#include "timetable/infra/txt_segments.hpp"
#include "timetable/infra/progress_bus.hpp"

#include <filesystem>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>

namespace timetable::infra {

	namespace {

		class SingleFileDataSource final : public io::DataSource {
		public:
			explicit SingleFileDataSource(io::DataFileSpec spec) : spec_(std::move(spec)) {}

			mathfp::Expected<timetable::domain::AssignmentInput> load() const override {
				using mathfp::fp::pipe::and_then;
				timetable::infra::progress::status("parsing: start");
				return
					txt::parse_segments_file(spec_.path)
					| and_then(txt::build_assignment_input);
			}

		private:
			io::DataFileSpec spec_;
		};

	}  // namespace

	mathfp::Expected<std::unique_ptr<io::DataSource>> make_single_file_data_source(
		io::DataFileSpec spec
	) {
		if (spec.path.empty())
			return mathfp::unexpected(mathfp::invalid_arg("data file path is empty"));

		if (!std::filesystem::exists(spec.path))
			return mathfp::unexpected(
				mathfp::invalid_arg("data file does not exist")
				.ctx("path", spec.path.string())
			);

		if (!std::filesystem::is_regular_file(spec.path))
			return mathfp::unexpected(
				mathfp::invalid_arg("data file is not a regular file")
				.ctx("path", spec.path.string())
			);

		return std::make_unique<SingleFileDataSource>(std::move(spec));
	}

}  // namespace timetable::infra
