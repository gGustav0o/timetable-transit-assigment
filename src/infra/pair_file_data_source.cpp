#include "timetable/infra/pair_file_data_source.hpp"

#include "timetable/infra/params_txt.hpp"
#include "timetable/infra/segments_csv.hpp"
#include "timetable/infra/txt_segments.hpp"

#include <filesystem>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>

namespace timetable::infra {

	namespace {

		class PairFileDataSource final : public io::DataSource {
		public:
			explicit PairFileDataSource(io::PairDataDirSpec spec) : spec_(std::move(spec)) {}

			mathfp::Expected<timetable::domain::AssignmentInput> load() const override {
				using mathfp::fp::pipe::and_then;
				using mathfp::fp::pipe::map;

				const auto segments_path = spec_.root / "connection_segments_input.csv";
				const auto params_path = spec_.root / "params.txt";

				return
					csv::parse_connection_segments_csv(segments_path)
					| and_then([](txt::SegmentColumns columns) {
						return txt::build_assignment_input(std::move(columns));
					})
					| and_then([params_path](timetable::domain::AssignmentInput input) {
						return params_txt::parse_search_params_file(params_path)
							| map([input = std::move(input)](timetable::domain::SearchParams params) mutable {
								input.params = std::move(params);
								return input;
							});
					});
			}

		private:
			io::PairDataDirSpec spec_;
		};

	}  // namespace

	mathfp::Expected<std::unique_ptr<io::DataSource>> make_pair_file_data_source(
		io::PairDataDirSpec spec
	) {
		if (spec.root.empty())
			return mathfp::unexpected(mathfp::invalid_arg("pair data dir path is empty"));

		if (!std::filesystem::exists(spec.root))
			return mathfp::unexpected(
				mathfp::invalid_arg("pair data dir does not exist")
				.ctx("path", spec.root.string())
			);

		if (!std::filesystem::is_directory(spec.root))
			return mathfp::unexpected(
				mathfp::invalid_arg("pair data dir is not a directory")
				.ctx("path", spec.root.string())
			);

		const auto segments_path = spec.root / "connection_segments_input.csv";
		const auto params_path = spec.root / "params.txt";
		if (!std::filesystem::exists(segments_path))
			return mathfp::unexpected(
				mathfp::invalid_arg("missing required connection_segments_input.csv")
				.ctx("path", segments_path.string())
			);
		if (!std::filesystem::exists(params_path))
			return mathfp::unexpected(
				mathfp::invalid_arg("missing required params.txt")
				.ctx("path", params_path.string())
			);

		return std::make_unique<PairFileDataSource>(std::move(spec));
	}

}  // namespace timetable::infra

