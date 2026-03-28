#include "timetable/infra/pair_file_data_source.hpp"

#include "timetable/domain/params_factory.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "timetable/infra/presegmented_input.hpp"
#include "timetable/infra/segments_csv.hpp"

#include <filesystem>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::infra {

	namespace {
		mathfp::Expected<timetable::domain::SearchParams> make_pair_default_search_params() {
			using namespace timetable::domain;

			MATHFP_TRY_LET(
				PreprocessParams
				, preprocess
				, make_preprocess_params(
					WalkCostKind::Time
					, WalkCostWeights{
						.w_time = Dimless{ 1.0 }
						, .w_length = Dimless{ 0.0 }
					}
					, std::nullopt
					, true
					, false
					, true
					, true
					, TimeAggregationKind::Mean
					, true
					, true
				)
			);
			MATHFP_TRY_LET(
				SearchImpedance
				, impedance
				, make_search_impedance(
					Dimless{ 1.0 }
					, Dimless{ 12.0 }
					, Dimless{ 0.0 }
				)
			);
			MATHFP_TRY_LET(
				TransferLimits
				, transfers
				, make_transfer_limits(
					TransferCount{ 5 }
					, Time{ 0.0 }
					, Time{ 30.0 }
					, true
					, true
				)
			);
			MATHFP_TRY_LET(
				SearchTolerances
				, search_tolerances
				, make_search_tolerances(
					Dimless{ 1.2 }
					, Dimless{ 10.0 }
					, Dimless{ 1.2 }
					, Dimless{ 10.0 }
					, Dimless{ 1.0 }
					, Dimless{ 1.0 }
				)
			);
			MATHFP_TRY_LET(
				ChoiceTolerances
				, choice_tolerances
				, make_choice_tolerances(
					Dimless{ 1.2 }
					, Dimless{ 10.0 }
					, Dimless{ 1.2 }
					, Dimless{ 10.0 }
					, Dimless{ 1.0 }
					, Dimless{ 1.0 }
				)
			);
			MATHFP_TRY_LET(
				SplitParams
				, split
				, make_split_params(
					Dimless{ 1.0 }
					, Dimless{ 1.0 }
					, Dimless{ 0.0 }
					, Dimless{ 4.0 }
					, Dimless{ 1.0 }
					, Dimless{ 1.0 }
					, Dimless{ 60.0 }
					, Dimless{ 0.3 }
					, Dimless{ 0.6 }
				)
			);

			return make_search_params(
				std::move(preprocess)
				, std::move(impedance)
				, std::move(transfers)
				, std::move(search_tolerances)
				, std::move(choice_tolerances)
				, std::move(split)
			);
		}

		class PairFileDataSource final : public io::DataSource {
		public:
			explicit PairFileDataSource(io::PairDataDirSpec spec) : spec_(std::move(spec)) {}

			mathfp::Expected<timetable::domain::AssignmentInput> load() const override {
				using mathfp::fp::pipe::and_then;
				using timetable::infra::LogLevel;
				using timetable::infra::progress::log;
				using timetable::infra::progress::status;

				const auto segments_path = spec_.root / "connection_segments_input.csv";

				status("parsing: loading pair input");
				return
					csv::parse_connection_segments_csv(segments_path)
					| and_then([](timetable::infra::SegmentColumns columns) {
						return build_presegmented_assignment_input(std::move(columns));
					})
					| and_then([&](timetable::domain::AssignmentInput input) -> mathfp::Expected<timetable::domain::AssignmentInput> {
						status("parsing: applying default params");
						log("parsing: params.txt is currently ignored; using built-in defaults", LogLevel::Warning);
						MATHFP_TRY_LET(
							timetable::domain::SearchParams
							, params
							, make_pair_default_search_params()
						);
						input.params = std::move(params);
						status("parsing: pair input ready");
						return mathfp::Expected<timetable::domain::AssignmentInput>(std::move(input));
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
		if (!std::filesystem::exists(segments_path))
			return mathfp::unexpected(
				mathfp::invalid_arg("missing required connection_segments_input.csv")
				.ctx("path", segments_path.string())
			);

		return std::make_unique<PairFileDataSource>(std::move(spec));
	}

}  // namespace timetable::infra
