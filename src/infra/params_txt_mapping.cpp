#include "detail/params_txt.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <mathfp/core/applicative.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/traverse.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/params_factory.hpp"

namespace timetable::infra::params_txt::detail {

	namespace {

		struct FieldSpec final {
			std::string_view key{};
			std::string_view path{};
		};

		template <std::size_t N>
		using DoubleArray = std::array<double, N>;

		template <std::size_t N>
		using ObjectArray = std::array<const Object*, N>;

		struct ParsedToleranceBundle final {
			timetable::domain::SearchTolerances search{};
			timetable::domain::ChoiceTolerances choice{};
		};

		struct ParsedSearchBundle final {
			timetable::domain::PreprocessParams preprocess{};
			timetable::domain::SearchImpedance impedance{};
			timetable::domain::TransferLimits transfers{};
		};

		template <class T, std::size_t N>
		std::array<T, N> to_array(std::vector<T> values) {
			std::array<T, N> out{};
			std::move(values.begin(), values.end(), out.begin());
			return out;
		}

		template <std::size_t N>
		mathfp::Expected<ObjectArray<N>> read_object_array(
			const Object& obj
			, const std::array<FieldSpec, N>& specs
		) {
			MATHFP_TRY_LET(
				std::vector<const Object*>
				, values
				, mathfp::trv::traverse(specs, [&](const auto& spec) {
					return object_at(obj, spec.key, spec.path);
				})
			);
			return to_array<const Object*, N>(std::move(values));
		}

		template <std::size_t N>
		mathfp::Expected<DoubleArray<N>> read_number_array(
			const Object& obj
			, const std::array<FieldSpec, N>& specs
		) {
			MATHFP_TRY_LET(
				std::vector<double>
				, values
				, mathfp::trv::traverse(specs, [&](const auto& spec) {
					return number_at(obj, spec.key, spec.path);
				})
			);
			return to_array<double, N>(std::move(values));
		}

		mathfp::Expected<timetable::domain::SearchTolerances> parse_search_tolerances(
			const Object& search_tol
		) {
			using namespace timetable::domain;

			constexpr auto specs = std::array{
				FieldSpec  { "minSearchImpFactor"      , "root.searchPara.ToleranceConstraints" }
				, FieldSpec{ "minSearchImpAbs"         , "root.searchPara.ToleranceConstraints" }
				, FieldSpec{ "minJourneyTimeFactor"    , "root.searchPara.ToleranceConstraints" }
				, FieldSpec{ "minJourneyTimeAbs"       , "root.searchPara.ToleranceConstraints" }
				, FieldSpec{ "minNumberTransfersFactor", "root.searchPara.ToleranceConstraints" }
				, FieldSpec{ "minNumberTransfersAbs"   , "root.searchPara.ToleranceConstraints" }
			};

			MATHFP_TRY_LET(DoubleArray<6>, values, read_number_array(search_tol, specs));
			const auto [imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add] = values;
			return make_search_tolerances(
				Dimless  { imp_mult }
				, Dimless{ imp_add }
				, Dimless{ jt_mult }
				, Dimless{ jt_add }
				, Dimless{ nt_mult }
				, Dimless{ nt_add }
			);
		}

		mathfp::Expected<timetable::domain::ChoiceTolerances> parse_choice_tolerances(
			const Object& choice_tol
		) {
			using namespace timetable::domain;

			constexpr auto specs = std::array{
				FieldSpec  { "minSearchImpFactor"      , "root.choicePara.ToleranceConstraints" }
				, FieldSpec{ "minSearchImpAbs"         , "root.choicePara.ToleranceConstraints" }
				, FieldSpec{ "minJourneyTimeFactor"    , "root.choicePara.ToleranceConstraints" }
				, FieldSpec{ "minJourneyTimeAbs"       , "root.choicePara.ToleranceConstraints" }
				, FieldSpec{ "minNumberTransfersFactor", "root.choicePara.ToleranceConstraints" }
				, FieldSpec{ "minNumberTransfersAbs"   , "root.choicePara.ToleranceConstraints" }
			};

			MATHFP_TRY_LET(DoubleArray<6>, values, read_number_array(choice_tol, specs));
			const auto [imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add] = values;
			return make_choice_tolerances(
				Dimless  { imp_mult }
				, Dimless{ imp_add }
				, Dimless{ jt_mult }
				, Dimless{ jt_add }
				, Dimless{ nt_mult }
				, Dimless{ nt_add }
			);
		}

		mathfp::Expected<timetable::domain::TransferLimits> parse_transfer_limits(
			const Object& search_para
			, const Object& temporal
		) {
			using namespace timetable::domain;

			constexpr auto search_specs = std::array{
				FieldSpec{ "maxNumTransfers", "root.searchPara" }
			};
			constexpr auto temporal_specs = std::array{
				FieldSpec  { "minTWT", "root.searchPara.TemporalSuitability" }
				, FieldSpec{ "maxTWT", "root.searchPara.TemporalSuitability" }
			};

			MATHFP_TRY_LET(DoubleArray<1>, search_values, read_number_array(search_para, search_specs));
			MATHFP_TRY_LET(DoubleArray<2>, temporal_values, read_number_array(temporal, temporal_specs));

			const auto [max_transfers] = search_values;
			const auto [min_twt, max_twt] = temporal_values;

			return make_transfer_limits(
				TransferCount{ static_cast<std::int32_t>(max_transfers) }
				, Time{ min_twt }
				, Time{ max_twt }
				, true
				, true
			);
		}

		mathfp::Expected<timetable::domain::SearchImpedance> parse_search_impedance(
			const Object& search_imp
		) {
			using namespace timetable::domain;

			constexpr auto specs = std::array{
				FieldSpec  { "inVehTimeFactor"   , "root.searchPara.SearchImp" }
				, FieldSpec{ "numTransfersFactor", "root.searchPara.SearchImp" }
				, FieldSpec{ "supplementsFactor" , "root.searchPara.SearchImp" }
			};

			MATHFP_TRY_LET(DoubleArray<3>, values, read_number_array(search_imp, specs));
			const auto [in_veh_factor, transfers_factor, supplements_factor] = values;
			return make_search_impedance(
				Dimless  { in_veh_factor }
				, Dimless{ transfers_factor }
				, Dimless{ supplements_factor }
			);
		}

		mathfp::Expected<timetable::domain::PreprocessParams> make_default_preprocess_params() {
			using namespace timetable::domain;

			return make_preprocess_params(
				WalkCostKind::Time
				, WalkCostWeights{
					.w_time     = Dimless{ 1.0 }
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
			);
		}

		mathfp::Expected<double> parse_choice_model_beta(
			const Object& split_para
		) {
			MATHFP_TRY_LET(std::string, choice_model, string_at(split_para, "choiceModel", "root.splitPara"));

			struct ChoiceModelSpec final {
				std::string_view model{};
				std::string_view exponent_key{};
			};

			constexpr auto specs = std::array{
				ChoiceModelSpec  { "Kirchhoff", "KirchhoffExp" }
				, ChoiceModelSpec{ "Logit"    , "logitExp" }
				, ChoiceModelSpec{ "Lohse"    , "LohseExp" }
			};

			const auto it = std::find_if(
				specs.begin()
				, specs.end()
				, [&](const ChoiceModelSpec& spec) {
					return spec.model == choice_model;
				}
			);
			if (it == specs.end()) {
				return mathfp::unexpected(
					mathfp::invalid_arg("unsupported choiceModel")
					.ctx("choiceModel", choice_model)
				);
			}

			return number_at(split_para, it->exponent_key, "root.splitPara");
		}

		mathfp::Expected<timetable::domain::SplitParams> parse_split_params(
			const Object& split_para
			, const Object& indep
			, const Object& split_imp
			, const Object& split_pjt
		) {
			using namespace timetable::domain;

			constexpr auto split_imp_specs = std::array{
				FieldSpec  { "perceivedJourneyTimeFactor" , "root.splitPara.SplitImp" }
				, FieldSpec{ "temporalUtilityFactor_early", "root.splitPara.SplitImp" }
				, FieldSpec{ "temporalUtilityFactor_late" , "root.splitPara.SplitImp" }
				, FieldSpec{ "fareFactor"                 , "root.splitPara.SplitImp" }
			};
			constexpr auto split_pjt_specs = std::array{
				FieldSpec  { "inVehTimeFactor"       , "root.splitPara.SplitImp.PerceivedJourneyTime" }
				, FieldSpec{ "transferWaitTimeFactor", "root.splitPara.SplitImp.PerceivedJourneyTime" }
				, FieldSpec{ "numTransfersFactor"    , "root.splitPara.SplitImp.PerceivedJourneyTime" }
			};
			constexpr auto indep_specs = std::array{
				FieldSpec  { "gamma"                  , "root.splitPara.Independence" }
				, FieldSpec{ "indepMaxDelta"          , "root.splitPara.Independence" }
				, FieldSpec{ "indepHigherQualityCoeff", "root.splitPara.Independence" }
				, FieldSpec{ "indepLowerQualityCoeff" , "root.splitPara.Independence" }
			};
			constexpr auto split_para_specs = std::array{
				FieldSpec{ "BoxCoxExp", "root.splitPara" }
			};

			MATHFP_TRY_LET(DoubleArray<4>, split_imp_values, read_number_array(split_imp, split_imp_specs));
			MATHFP_TRY_LET(DoubleArray<3>, split_pjt_values, read_number_array(split_pjt, split_pjt_specs));
			MATHFP_TRY_LET(DoubleArray<4>, indep_values, read_number_array(indep, indep_specs));
			MATHFP_TRY_LET(DoubleArray<1>, split_para_values, read_number_array(split_para, split_para_specs));
			MATHFP_TRY_LET(double, beta, parse_choice_model_beta(split_para));

			const auto [q_time, q_dep_early, q_dep_late, q_fare] = split_imp_values;
			const auto [pjt_journey_time, pjt_transfer_time, pjt_transfer_count] = split_pjt_values;
			const auto [gamma, temporal_similarity_scale, higher_quality_scale, lower_quality_scale] = indep_values;
			const auto [boxcox_t] = split_para_values;

			return make_split_params(
				Dimless{ q_time }
				, Dimless{ 1.0 }
				, Dimless{ q_fare }
				, PerceivedJourneyTimeWeights{
					.journey_time     = Dimless{ pjt_journey_time }
					, .transfer_time  = Dimless{ pjt_transfer_time }
					, .transfer_count = Dimless{ pjt_transfer_count }
				}
				, TemporalUtilityWeights{
					.early_departure  = Dimless{ q_dep_early }
					, .late_departure = Dimless{ q_dep_late }
				}
				, Dimless{ beta }
				, Dimless{ boxcox_t }
				, Dimless{ gamma }
				, Dimless{ temporal_similarity_scale }
				, Dimless{ higher_quality_scale }
				, Dimless{ lower_quality_scale }
			);
		}

	}  // namespace

	mathfp::Expected<timetable::domain::SearchParams> map_params(
		const Object& root
	) {
		using namespace timetable::domain;

		constexpr auto root_specs = std::array{
			FieldSpec  { "searchPara", "root" }
			, FieldSpec{ "choicePara", "root" }
			, FieldSpec{ "splitPara" , "root" }
		};
		constexpr auto search_specs = std::array{
			FieldSpec  { "ToleranceConstraints", "root.searchPara" }
			, FieldSpec{ "TemporalSuitability" , "root.searchPara" }
			, FieldSpec{ "SearchImp"           , "root.searchPara" }
		};
		constexpr auto choice_specs = std::array{
			FieldSpec{ "ToleranceConstraints", "root.choicePara" }
		};
		constexpr auto split_specs = std::array{
			FieldSpec  { "Independence", "root.splitPara" }
			, FieldSpec{ "SplitImp"    , "root.splitPara" }
		};
		constexpr auto split_imp_specs = std::array{
			FieldSpec{ "PerceivedJourneyTime", "root.splitPara.SplitImp" }
		};

		MATHFP_TRY_LET(ObjectArray<3>, root_objects, read_object_array(root, root_specs));
		const auto [search_para, choice_para, split_para] = root_objects;

		MATHFP_TRY_LET(ObjectArray<3>, search_objects, read_object_array(*search_para, search_specs));
		const auto [search_tol, temporal, search_imp] = search_objects;

		MATHFP_TRY_LET(ObjectArray<1>, choice_objects, read_object_array(*choice_para, choice_specs));
		const auto [choice_tol] = choice_objects;

		MATHFP_TRY_LET(ObjectArray<2>, split_objects, read_object_array(*split_para, split_specs));
		const auto [indep, split_imp] = split_objects;

		MATHFP_TRY_LET(ObjectArray<1>, split_imp_objects, read_object_array(*split_imp, split_imp_specs));
		const auto [split_pjt] = split_imp_objects;

		MATHFP_TRY_LET(
			ParsedToleranceBundle
			, tolerances
			, mathfp::app::lift2(
				[](SearchTolerances search, ChoiceTolerances choice) {
					return ParsedToleranceBundle{
						.search = std::move(search)
						, .choice = std::move(choice)
					};
				}
				, parse_search_tolerances(*search_tol)
				, parse_choice_tolerances(*choice_tol)
			)
		);

		MATHFP_TRY_LET(
			ParsedSearchBundle
			, search_bundle
			, mathfp::app::lift3(
				[](PreprocessParams preprocess, SearchImpedance impedance, TransferLimits transfers) {
					return ParsedSearchBundle{
						.preprocess = std::move(preprocess)
						, .impedance = std::move(impedance)
						, .transfers = std::move(transfers)
					};
				}
				, make_default_preprocess_params()
				, parse_search_impedance(*search_imp)
				, parse_transfer_limits(*search_para, *temporal)
			)
		);
		MATHFP_TRY_LET(SplitParams, split, parse_split_params(*split_para, *indep, *split_imp, *split_pjt));

		return make_search_params(
			std::move(search_bundle.preprocess)
			, std::move(search_bundle.impedance)
			, std::move(search_bundle.transfers)
			, std::move(tolerances.search)
			, std::move(tolerances.choice)
			, std::move(split)
		);
	}

}  // namespace timetable::infra::params_txt::detail
