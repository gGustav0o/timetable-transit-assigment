#include "timetable/infra/params_txt.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <tao/pegtl.hpp>

#include <mathfp/core/applicative.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/traverse.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/domain/params_factory.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "timetable/infra/text_parse.hpp"

namespace timetable::infra::params_txt {

	namespace {

		struct Value;
		using Object = std::map<std::string, Value>;
		using Array  = std::vector<Value>;

		struct Value final {
			std::variant<std::nullptr_t, bool, double, std::string, Object, Array> data{};
		};

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

		namespace pegtl = tao::pegtl;

		struct ContainerState final {
			enum class Kind : std::uint8_t {
				Object
				, Array
			};

			Kind   kind{};
			Object object{};
			Array  array{};
			std::optional<std::string> pending_key{};
		};

		struct ParserState final {
			std::vector<ContainerState> stack{};
			std::optional<Value> root{};
		};

		mathfp::Expected<ParserState> with_object_key(
			ParserState&& state
			, std::string key
		) {
			if (state.stack.empty() || state.stack.back().kind != ContainerState::Kind::Object) {
				return mathfp::unexpected(
					mathfp::invalid_arg("object key outside object")
				);
			}
			auto& top = state.stack.back();
			if (top.pending_key.has_value()) {
				return mathfp::unexpected(
					mathfp::invalid_arg("dangling object key before value")
				);
			}
			top.pending_key = std::move(key);
			return std::move(state);
		}

		mathfp::Expected<ParserState> with_pushed_value(
			ParserState&& state
			, Value value
		) {
			if (state.stack.empty()) {
				if (state.root.has_value()) {
					return mathfp::unexpected(
						mathfp::invalid_arg("multiple root values")
					);
				}
				state.root = std::move(value);
				return std::move(state);
			}

			auto& top = state.stack.back();
			if (top.kind == ContainerState::Kind::Array) {
				top.array.push_back(std::move(value));
				return std::move(state);
			}

			if (!top.pending_key.has_value()) {
				return mathfp::unexpected(
					mathfp::invalid_arg("object value without key")
				);
			}

			top.object.emplace(std::move(*top.pending_key), std::move(value));
			top.pending_key.reset();
			return std::move(state);
		}

		ParserState begun_object(ParserState&& state) {
			state.stack.push_back(ContainerState{ .kind = ContainerState::Kind::Object });
			return std::move(state);
		}

		mathfp::Expected<ParserState> ended_object(ParserState&& state) {
			if (state.stack.empty() || state.stack.back().kind != ContainerState::Kind::Object) {
				return mathfp::unexpected(
					mathfp::invalid_arg("unexpected object end")
				);
			}
			auto ctx = std::move(state.stack.back());
			state.stack.pop_back();
			if (ctx.pending_key.has_value()) {
				return mathfp::unexpected(
					mathfp::invalid_arg("dangling object key at object end")
				);
			}
			return with_pushed_value(std::move(state), Value{ std::move(ctx.object) });
		}

		ParserState begun_array(ParserState&& state) {
			state.stack.push_back(ContainerState{ .kind = ContainerState::Kind::Array });
			return std::move(state);
		}

		mathfp::Expected<ParserState> ended_array(ParserState&& state) {
			if (state.stack.empty() || state.stack.back().kind != ContainerState::Kind::Array) {
				return mathfp::unexpected(
					mathfp::invalid_arg("unexpected array end")
				);
			}
			auto ctx = std::move(state.stack.back());
			state.stack.pop_back();
			return with_pushed_value(std::move(state), Value{ std::move(ctx.array) });
		}

		template <typename Input, typename Transition>
		void apply_parser_transition(
			ParserState& state
			, const Input& in
			, Transition&& transition
		) {
			auto next_state = transition(std::move(state));
			if (!next_state) {
				throw pegtl::parse_error(next_state.error().message(), in);
			}

			state = std::move(*next_state);
		}

		std::string decode_quoted_string(std::string_view token) {
			std::string out;
			if (token.size() < 2) return out;
			out.reserve(token.size() - 2);

			for (std::size_t i = 1; i + 1 < token.size(); ++i) {
				const auto c = token[i];
				if (c != '\\') {
					out.push_back(c);
					continue;
				}

				if (i + 2 >= token.size()) {
					break;
				}

				const auto e = token[++i];
				switch (e) {
					case '\\': out.push_back('\\'); break;
					case '\'': out.push_back('\''); break;
					case 'n':  out.push_back('\n'); break;
					case 'r':  out.push_back('\r'); break;
					case 't':  out.push_back('\t'); break;
					default:   out.push_back(e);    break;
				}
			}
			return out;
		}

		namespace grammar {

			struct ws : pegtl::star<pegtl::space> {};

			struct object_begin : pegtl::one<'{'> {};
			struct object_end   : pegtl::one<'}'> {};
			struct array_begin  : pegtl::one<'['> {};
			struct array_end    : pegtl::one<']'> {};
			struct comma        : pegtl::one<','> {};
			struct colon        : pegtl::one<':'> {};

			struct escaped_char  : pegtl::seq<pegtl::one<'\\'>, pegtl::any> {};
			struct plain_char    : pegtl::not_one<'\\', '\''> {};
			struct string_body   : pegtl::star<pegtl::sor<escaped_char, plain_char>> {};
			struct quoted_string : pegtl::if_must<pegtl::one<'\''>, string_body, pegtl::one<'\''>> {};

			struct key_string   : quoted_string {};
			struct value_string : quoted_string {};

			struct int_part       : pegtl::plus<pegtl::digit> {};
			struct frac_part      : pegtl::seq<pegtl::one<'.'>, pegtl::star<pegtl::digit>> {};
			struct dot_frac_only  : pegtl::seq<pegtl::one<'.'>, pegtl::plus<pegtl::digit>> {};

			struct number_mantissa : pegtl::sor<
				pegtl::seq<int_part, pegtl::opt<frac_part>>
				, dot_frac_only
			> {};
			struct number_exponent : pegtl::seq<
				pegtl::one<'e', 'E'>
				, pegtl::opt<pegtl::one<'+', '-'>>
				, pegtl::plus<pegtl::digit>
			> {};
			struct number : pegtl::seq<
				pegtl::opt<pegtl::one<'-'>>
				, number_mantissa
				, pegtl::opt<number_exponent>
			> {};

			struct kw_true  : TAO_PEGTL_STRING("True") {};
			struct kw_false : TAO_PEGTL_STRING("False") {};
			struct kw_none  : TAO_PEGTL_STRING("None") {};

			struct value;
			struct member      : pegtl::seq<ws, key_string, ws, colon, ws, value> {};
			struct member_tail : pegtl::seq<ws, comma, ws> {};
			struct members     : pegtl::list_must<member, member_tail> {};
			struct object      : pegtl::seq<object_begin, ws, pegtl::opt<members>, ws, object_end> {};

			struct elements    : pegtl::list_must<value, member_tail> {};
			struct array       : pegtl::seq<array_begin, ws, pegtl::opt<elements>, ws, array_end> {};

			struct value       : pegtl::sor<object, array, value_string, number, kw_true, kw_false, kw_none> {};
			struct start       : pegtl::must<ws, value, ws, pegtl::eof> {};

		}  // namespace grammar

		template <typename Rule>
		struct action final : pegtl::nothing<Rule> {};

		template <>
		struct action<grammar::object_begin> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				(void)in;
				state = begun_object(std::move(state));
			}
		};

		template <>
		struct action<grammar::object_end> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				apply_parser_transition(state, in, ended_object);
			}
		};

		template <>
		struct action<grammar::array_begin> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				(void)in;
				state = begun_array(std::move(state));
			}
		};

		template <>
		struct action<grammar::array_end> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				apply_parser_transition(state, in, ended_array);
			}
		};

		template <>
		struct action<grammar::key_string> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				apply_parser_transition(
					state
					, in
					, [&](ParserState current) {
						return with_object_key(
							std::move(current)
							, decode_quoted_string(in.string_view())
						);
					}
				);
			}
		};

		template <>
		struct action<grammar::value_string> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				apply_parser_transition(
					state
					, in
					, [&](ParserState current) {
						return with_pushed_value(
							std::move(current)
							, Value{ decode_quoted_string(in.string_view()) }
						);
					}
				);
			}
		};

		template <>
		struct action<grammar::kw_true> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				apply_parser_transition(
					state
					, in
					, [](ParserState current) {
						return with_pushed_value(std::move(current), Value{ true });
					}
				);
			}
		};

		template <>
		struct action<grammar::kw_false> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				apply_parser_transition(
					state
					, in
					, [](ParserState current) {
						return with_pushed_value(std::move(current), Value{ false });
					}
				);
			}
		};

		template <>
		struct action<grammar::kw_none> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				apply_parser_transition(
					state
					, in
					, [](ParserState current) {
						return with_pushed_value(std::move(current), Value{ nullptr });
					}
				);
			}
		};

		template <>
		struct action<grammar::number> final {
			template <typename Input>
			static void apply(const Input& in, ParserState& state) {
				const auto result = text_parse::parse_numeric_token<double>(
					in.string_view()
					, [](const char* begin, char** end) {
						return std::strtod(begin, end);
					}
				);

				if (const auto* failure = std::get_if<text_parse::NumericParseFailure>(&result)) {
					if (*failure == text_parse::NumericParseFailure::Range) {
						throw pegtl::parse_error("number out of range", in);
					}
					throw pegtl::parse_error("failed to parse number", in);
				}

				const auto value = std::get<double>(result);
				apply_parser_transition(
					state
					, in
					, [&](ParserState current) {
						return with_pushed_value(std::move(current), Value{ value });
					}
				);
			}
		};

		mathfp::Expected<Value> parse_value_text(
			const std::string& text
			, std::string_view source_name
		) {
			pegtl::memory_input in(text, source_name);
			ParserState state;
			try {
				pegtl::parse<grammar::start, action>(in, state);
				if (!state.stack.empty() || !state.root.has_value()) {
					return mathfp::unexpected(
						mathfp::invalid_arg("incomplete parse state")
						.ctx("source", std::string(source_name))
					);
				}
				return std::move(*state.root);
			} catch (const pegtl::parse_error& e) {
				auto err = mathfp::invalid_arg(e.what());
				err.ctx("source", std::string(source_name));
				if (!e.positions().empty()) {
					const auto& pos = e.positions().front();
					err.ctx("line", static_cast<std::int64_t>(pos.line));
					err.ctx("column", static_cast<std::int64_t>(pos.column));
				}
				return mathfp::unexpected(std::move(err));
			}
		}

		mathfp::Expected<const Object*> as_object(
			const Value& v
			, std::string_view path
		) {
			if (!std::holds_alternative<Object>(v.data)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("expected object")
					.ctx("path", std::string(path))
				);
			}
			return &std::get<Object>(v.data);
		}

		mathfp::Expected<const Value*> object_get(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			const auto it = obj.find(std::string(key));
			if (it == obj.end()) {
				return mathfp::unexpected(
					mathfp::invalid_arg("missing required key")
					.ctx("path", std::string(path))
					.ctx("key", std::string(key))
				);
			}
			return &it->second;
		}

		mathfp::Expected<const Object*> object_at(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			MATHFP_TRY_LET(const Value*, child, object_get(obj, key, path));
			const auto child_path = std::string(path) + "." + std::string(key);
			return as_object(*child, child_path);
		}

		mathfp::Expected<double> number_at(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			MATHFP_TRY_LET(const Value*, child, object_get(obj, key, path));
			if (!std::holds_alternative<double>(child->data)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("expected number")
					.ctx("path", std::string(path))
					.ctx("key", std::string(key))
				);
			}
			return std::get<double>(child->data);
		}

		mathfp::Expected<bool> bool_at(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			MATHFP_TRY_LET(const Value*, child, object_get(obj, key, path));
			if (!std::holds_alternative<bool>(child->data)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("expected bool")
					.ctx("path", std::string(path))
					.ctx("key", std::string(key))
				);
			}
			return std::get<bool>(child->data);
		}

		mathfp::Expected<std::string> string_at(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			MATHFP_TRY_LET(const Value*, child, object_get(obj, key, path));
			if (!std::holds_alternative<std::string>(child->data)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("expected string")
					.ctx("path", std::string(path))
					.ctx("key", std::string(key))
				);
			}
			return std::get<std::string>(child->data);
		}

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

			MATHFP_TRY_LET(DoubleArray<1>, search_values  , read_number_array(search_para, search_specs));
			MATHFP_TRY_LET(DoubleArray<2>, temporal_values, read_number_array(temporal, temporal_specs));

			const auto [max_transfers]    = search_values;
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

			MATHFP_TRY_LET(DoubleArray<4>, split_imp_values , read_number_array(split_imp , split_imp_specs));
			MATHFP_TRY_LET(DoubleArray<3>, split_pjt_values , read_number_array(split_pjt , split_pjt_specs));
			MATHFP_TRY_LET(DoubleArray<4>, indep_values     , read_number_array(indep     , indep_specs));
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

	}  // namespace

	mathfp::Expected<timetable::domain::SearchParams> parse_search_params_file(
		const std::filesystem::path& path
	) {
		using timetable::infra::LogLevel;
		using timetable::infra::progress::log;
		using timetable::infra::progress::status;

		status("parsing: opening params.txt");
		std::ifstream input(path);
		if (!input.is_open()) {
			return mathfp::unexpected(
				mathfp::invalid_arg("failed to open params file")
				.ctx("path", path.string())
			);
		}

		const std::string text(
			(std::istreambuf_iterator<char>(input))
			, std::istreambuf_iterator<char>()
		);
		log(
			fmt::format(
				"parsing: params.txt loaded; bytes = {}"
				, text.size()
			)
			, LogLevel::Info
		);

		status("parsing: parsing params.txt syntax");
		MATHFP_TRY_LET(Value, root, parse_value_text(text, path.string()));
		log("parsing: params.txt syntax parsed", LogLevel::Info);

		status("parsing: validating params.txt root");
		MATHFP_TRY_LET(const Object*, root_obj, as_object(root, "root"));
		log("parsing: params.txt root object validated", LogLevel::Info);

		status("parsing: mapping params");
		MATHFP_TRY_LET(
			timetable::domain::SearchParams
			, params
			, map_params(*root_obj)
		);
		log(
			fmt::format(
				"parsing: params mapped; max_transfers = {}  allow_start_wait = {}  allow_end_wait = {}"
				, params.transfers.max_transfers.get()
				, params.transfers.allow_start_wait ? "true" : "false"
				, params.transfers.allow_end_wait ? "true" : "false"
			)
			, LogLevel::Info
		);
		status("parsing: params.txt parsed");
		return params;
	}

}  // namespace timetable::infra::params_txt
