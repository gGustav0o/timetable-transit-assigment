#include "timetable/infra/params_txt.hpp"

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

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/domain/params_factory.hpp"
#include "timetable/domain/state_ops.hpp"
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
			timetable::domain::state_ops::transition_or_throw(
				state
				, std::forward<Transition>(transition)
				, [&](const mathfp::Error& error) {
					throw pegtl::parse_error(error.message(), in);
				}
			);
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
			struct member : pegtl::seq<ws, key_string, ws, colon, ws, value> {};
			struct member_tail : pegtl::seq<ws, comma, ws> {};
			struct members : pegtl::list_must<member, member_tail> {};
			struct object : pegtl::seq<object_begin, ws, pegtl::opt<members>, ws, object_end> {};

			struct elements : pegtl::list_must<value, member_tail> {};
			struct array : pegtl::seq<array_begin, ws, pegtl::opt<elements>, ws, array_end> {};

			struct value : pegtl::sor<object, array, value_string, number, kw_true, kw_false, kw_none> {};
			struct start : pegtl::must<ws, value, ws, pegtl::eof> {};

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
							std::move(current),
							decode_quoted_string(in.string_view())
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
							std::move(current),
							Value{ decode_quoted_string(in.string_view()) }
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

		mathfp::Expected<timetable::domain::SearchParams> map_params(
			const Object& root
		) {
			using namespace timetable::domain;

			MATHFP_TRY_LET(const Object*, search_para, object_at(root, "searchPara", "root"));
			MATHFP_TRY_LET(const Object*, choice_para, object_at(root, "choicePara", "root"));
			MATHFP_TRY_LET(const Object*, split_para, object_at(root, "splitPara", "root"));

			MATHFP_TRY_LET(const Object*, search_tol, object_at(*search_para, "ToleranceConstraints", "root.searchPara"));
			MATHFP_TRY_LET(const Object*, temporal, object_at(*search_para, "TemporalSuitability", "root.searchPara"));
			MATHFP_TRY_LET(const Object*, search_imp, object_at(*search_para, "SearchImp", "root.searchPara"));

			MATHFP_TRY_LET(const Object*, choice_tol, object_at(*choice_para, "ToleranceConstraints", "root.choicePara"));

			MATHFP_TRY_LET(const Object*, indep, object_at(*split_para, "Independence", "root.splitPara"));
			MATHFP_TRY_LET(const Object*, split_imp, object_at(*split_para, "SplitImp", "root.splitPara"));

			const auto num = [](const Object& obj, std::string_view key, std::string_view path) {
				return number_at(obj, key, path);
			};
			const auto text = [](const Object& obj, std::string_view key, std::string_view path) {
				return string_at(obj, key, path);
			};

			MATHFP_TRY_LET(double, s_imp_mult, num(*search_tol, "minSearchImpFactor", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_imp_add, num(*search_tol, "minSearchImpAbs", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_jt_mult, num(*search_tol, "minJourneyTimeFactor", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_jt_add, num(*search_tol, "minJourneyTimeAbs", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_nt_mult, num(*search_tol, "minNumberTransfersFactor", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_nt_add, num(*search_tol, "minNumberTransfersAbs", "root.searchPara.ToleranceConstraints"));

			MATHFP_TRY_LET(SearchTolerances, search_tolerances, make_search_tolerances(
				Dimless{ s_imp_mult }
				, Dimless{ s_imp_add }
				, Dimless{ s_jt_mult }
				, Dimless{ s_jt_add }
				, Dimless{ s_nt_mult }
				, Dimless{ s_nt_add }
			));

			MATHFP_TRY_LET(double, c_imp_mult, num(*choice_tol, "minSearchImpFactor", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_imp_add, num(*choice_tol, "minSearchImpAbs", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_jt_mult, num(*choice_tol, "minJourneyTimeFactor", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_jt_add, num(*choice_tol, "minJourneyTimeAbs", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_nt_mult, num(*choice_tol, "minNumberTransfersFactor", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_nt_add, num(*choice_tol, "minNumberTransfersAbs", "root.choicePara.ToleranceConstraints"));

			MATHFP_TRY_LET(ChoiceTolerances, choice_tolerances, make_choice_tolerances(
				Dimless{ c_imp_mult }
				, Dimless{ c_imp_add }
				, Dimless{ c_jt_mult }
				, Dimless{ c_jt_add }
				, Dimless{ c_nt_mult }
				, Dimless{ c_nt_add }
			));

			MATHFP_TRY_LET(double, max_transfers, num(*search_para, "maxNumTransfers", "root.searchPara"));
			MATHFP_TRY_LET(double, min_twt, num(*temporal, "minTWT", "root.searchPara.TemporalSuitability"));
			MATHFP_TRY_LET(double, max_twt, num(*temporal, "maxTWT", "root.searchPara.TemporalSuitability"));
			MATHFP_TRY_LET(double, n_transfers, num(*search_imp, "numTransfersFactor", "root.searchPara.SearchImp"));

			MATHFP_TRY_LET(TransferLimits, transfer_limits, make_transfer_limits(
				TransferCount{ static_cast<std::int32_t>(max_transfers) }
				, Time{ min_twt }
				, Time{ max_twt }
				, true
				, true
			));

			MATHFP_TRY_LET(double, in_veh_factor, num(*search_imp, "inVehTimeFactor", "root.searchPara.SearchImp"));
			MATHFP_TRY_LET(double, suppl_factor, num(*search_imp, "supplementsFactor", "root.searchPara.SearchImp"));
			MATHFP_TRY_LET(SearchImpedance, impedance, make_search_impedance(
				Dimless{ in_veh_factor }
				, Dimless{ n_transfers }
				, Dimless{ suppl_factor }
				, Time{ n_transfers }
			));

			MATHFP_TRY_LET(PreprocessParams, preprocess, make_preprocess_params(
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
			));

			MATHFP_TRY_LET(std::string, choice_model, text(*split_para, "choiceModel", "root.splitPara"));
			double beta = 0.0;
			if (choice_model == "Kirchhoff") {
				MATHFP_TRY_LET(double, beta_v, num(*split_para, "KirchhoffExp", "root.splitPara"));
				beta = beta_v;
			} else if (choice_model == "Logit") {
				MATHFP_TRY_LET(double, beta_v, num(*split_para, "logitExp", "root.splitPara"));
				beta = beta_v;
			} else if (choice_model == "Lohse") {
				MATHFP_TRY_LET(double, beta_v, num(*split_para, "LohseExp", "root.splitPara"));
				beta = beta_v;
			} else {
				return mathfp::unexpected(
					mathfp::invalid_arg("unsupported choiceModel")
					.ctx("choiceModel", choice_model)
				);
			}

			MATHFP_TRY_LET(double, q_time, num(*split_imp, "perceivedJourneyTimeFactor", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, q_dep_early, num(*split_imp, "temporalUtilityFactor_early", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, q_dep_late, num(*split_imp, "temporalUtilityFactor_late", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, q_fare, num(*split_imp, "fareFactor", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, boxcox_t, num(*split_para, "BoxCoxExp", "root.splitPara"));
			MATHFP_TRY_LET(double, gamma, num(*indep, "gamma", "root.splitPara.Independence"));
			MATHFP_TRY_LET(double, x_scale, num(*indep, "indepMaxDelta", "root.splitPara.Independence"));
			MATHFP_TRY_LET(double, y_scale, num(*indep, "indepHigherQualityCoeff", "root.splitPara.Independence"));
			MATHFP_TRY_LET(double, z_scale, num(*indep, "indepLowerQualityCoeff", "root.splitPara.Independence"));
			MATHFP_TRY_LET(SplitParams, split, make_split_params(
				Dimless{ q_time }
				, Dimless{ 0.5 * (q_dep_early + q_dep_late) }
				, Dimless{ q_fare }
				, Dimless{ beta }
				, Dimless{ boxcox_t }
				, Dimless{ gamma }
				, Dimless{ x_scale }
				, Dimless{ y_scale }
				, Dimless{ z_scale }
			));

			return make_search_params(
				preprocess
				, impedance
				, transfer_limits
				, search_tolerances
				, choice_tolerances
				, split
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
			),
			LogLevel::Info
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
			),
			LogLevel::Info
		);
		status("parsing: params.txt parsed");
		return params;
	}

}  // namespace timetable::infra::params_txt
