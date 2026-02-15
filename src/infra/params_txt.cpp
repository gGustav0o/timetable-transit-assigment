#include "timetable/infra/params_txt.hpp"

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/params_factory.hpp"

namespace timetable::infra::params_txt {

	namespace {

		struct Value final;
		using Object = std::map<std::string, Value>;
		using Array = std::vector<Value>;

		struct Value final {
			std::variant<std::nullptr_t, bool, double, std::string, Object, Array> data{};
		};

		class Parser final {
		public:
			explicit Parser(std::string text) : text_(std::move(text)) {}

			mathfp::Expected<Value> parse() {
				skip_ws();
				const auto value = parse_value();
				if (!value) return mathfp::unexpected(value.error());
				skip_ws();
				if (!eof()) {
					return error("unexpected trailing characters");
				}
				return *value;
			}

		private:
			mathfp::Unexpected error(const char* msg) const {
				return mathfp::unexpected(
					mathfp::invalid_arg(msg)
					.ctx("offset", static_cast<std::int64_t>(pos_))
				);
			}

			bool eof() const noexcept { return pos_ >= text_.size(); }

			char peek() const noexcept {
				if (eof()) return '\0';
				return text_[pos_];
			}

			char get() noexcept {
				if (eof()) return '\0';
				return text_[pos_++];
			}

			void skip_ws() noexcept {
				while (!eof() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
					++pos_;
				}
			}

			bool match_literal(std::string_view lit) {
				if (text_.substr(pos_, lit.size()) == lit) {
					pos_ += lit.size();
					return true;
				}
				return false;
			}

			mathfp::Expected<Value> parse_value() {
				skip_ws();
				if (eof()) return error("unexpected end of input");

				const auto c = peek();
				if (c == '{') return parse_object();
				if (c == '[') return parse_array();
				if (c == '\'') return parse_string();
				if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return parse_number();
				if (match_literal("True")) return Value{ true };
				if (match_literal("False")) return Value{ false };
				if (match_literal("None")) return Value{ nullptr };

				return error("unexpected token");
			}

			mathfp::Expected<Value> parse_string() {
				if (get() != '\'') return error("expected quote");
				std::string out;

				while (!eof()) {
					const auto c = get();
					if (c == '\'') {
						return Value{ std::move(out) };
					}
					if (c == '\\') {
						if (eof()) return error("unterminated escape sequence");
						const auto e = get();
						switch (e) {
							case '\\': out.push_back('\\'); break;
							case '\'': out.push_back('\''); break;
							case 'n': out.push_back('\n'); break;
							case 'r': out.push_back('\r'); break;
							case 't': out.push_back('\t'); break;
							default: out.push_back(e); break;
						}
						continue;
					}
					out.push_back(c);
				}
				return error("unterminated string");
			}

			mathfp::Expected<Value> parse_number() {
				const auto start = pos_;
				if (peek() == '-') {
					++pos_;
				}

				bool has_digits = false;
				while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
					++pos_;
					has_digits = true;
				}
				if (!eof() && peek() == '.') {
					++pos_;
					while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
						++pos_;
						has_digits = true;
					}
				}
				if (!eof() && (peek() == 'e' || peek() == 'E')) {
					++pos_;
					if (!eof() && (peek() == '+' || peek() == '-')) ++pos_;
					while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) {
						++pos_;
						has_digits = true;
					}
				}

				if (!has_digits) return error("invalid number");

				const auto token = text_.substr(start, pos_ - start);
				errno = 0;
				char* end = nullptr;
				const auto value = std::strtod(token.c_str(), &end);
				if (end == token.c_str() || *end != '\0') {
					return error("failed to parse number");
				}
				if (errno == ERANGE) {
					return error("number out of range");
				}
				return Value{ value };
			}

			mathfp::Expected<Value> parse_array() {
				if (get() != '[') return error("expected '['");
				Array arr;
				skip_ws();
				if (peek() == ']') {
					get();
					return Value{ std::move(arr) };
				}

				while (true) {
					const auto value = parse_value();
					if (!value) return mathfp::unexpected(value.error());
					arr.push_back(*value);

					skip_ws();
					const auto c = get();
					if (c == ']') break;
					if (c != ',') return error("expected ',' or ']'");
					skip_ws();
				}

				return Value{ std::move(arr) };
			}

			mathfp::Expected<Value> parse_object() {
				if (get() != '{') return error("expected '{'");
				Object obj;
				skip_ws();
				if (peek() == '}') {
					get();
					return Value{ std::move(obj) };
				}

				while (true) {
					skip_ws();
					const auto key = parse_string();
					if (!key) return mathfp::unexpected(key.error());
					if (!std::holds_alternative<std::string>(key->data)) {
						return error("object key must be string");
					}
					const auto& key_str = std::get<std::string>(key->data);

					skip_ws();
					if (get() != ':') return error("expected ':'");
					skip_ws();

					const auto value = parse_value();
					if (!value) return mathfp::unexpected(value.error());
					obj.emplace(key_str, *value);

					skip_ws();
					const auto c = get();
					if (c == '}') break;
					if (c != ',') return error("expected ',' or '}'");
					skip_ws();
				}

				return Value{ std::move(obj) };
			}

		private:
			std::string text_{};
			std::size_t pos_{ 0 };
		};

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
			const auto child = object_get(obj, key, path);
			if (!child) return mathfp::unexpected(child.error());
			const auto child_path = std::string(path) + "." + std::string(key);
			return as_object(**child, child_path);
		}

		mathfp::Expected<double> number_at(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			const auto child = object_get(obj, key, path);
			if (!child) return mathfp::unexpected(child.error());
			if (!std::holds_alternative<double>((*child)->data)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("expected number")
					.ctx("path", std::string(path))
					.ctx("key", std::string(key))
				);
			}
			return std::get<double>((*child)->data);
		}

		mathfp::Expected<bool> bool_at(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			const auto child = object_get(obj, key, path);
			if (!child) return mathfp::unexpected(child.error());
			if (!std::holds_alternative<bool>((*child)->data)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("expected bool")
					.ctx("path", std::string(path))
					.ctx("key", std::string(key))
				);
			}
			return std::get<bool>((*child)->data);
		}

		mathfp::Expected<std::string> string_at(
			const Object& obj
			, std::string_view key
			, std::string_view path
		) {
			const auto child = object_get(obj, key, path);
			if (!child) return mathfp::unexpected(child.error());
			if (!std::holds_alternative<std::string>((*child)->data)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("expected string")
					.ctx("path", std::string(path))
					.ctx("key", std::string(key))
				);
			}
			return std::get<std::string>((*child)->data);
		}

		mathfp::Expected<timetable::domain::SearchParams> map_params(
			const Object& root
		) {
			using namespace timetable::domain;

			const auto search_para = object_at(root, "searchPara", "root");
			if (!search_para) return mathfp::unexpected(search_para.error());
			const auto choice_para = object_at(root, "choicePara", "root");
			if (!choice_para) return mathfp::unexpected(choice_para.error());
			const auto split_para = object_at(root, "splitPara", "root");
			if (!split_para) return mathfp::unexpected(split_para.error());

			const auto search_tol = object_at(**search_para, "ToleranceConstraints", "root.searchPara");
			if (!search_tol) return mathfp::unexpected(search_tol.error());
			const auto temporal = object_at(**search_para, "TemporalSuitability", "root.searchPara");
			if (!temporal) return mathfp::unexpected(temporal.error());
			const auto search_imp = object_at(**search_para, "SearchImp", "root.searchPara");
			if (!search_imp) return mathfp::unexpected(search_imp.error());

			const auto choice_tol = object_at(**choice_para, "ToleranceConstraints", "root.choicePara");
			if (!choice_tol) return mathfp::unexpected(choice_tol.error());

			const auto indep = object_at(**split_para, "Independence", "root.splitPara");
			if (!indep) return mathfp::unexpected(indep.error());
			const auto split_imp = object_at(**split_para, "SplitImp", "root.splitPara");
			if (!split_imp) return mathfp::unexpected(split_imp.error());

			const auto num = [](const Object& obj, std::string_view key, std::string_view path) {
				return number_at(obj, key, path);
			};
			const auto text = [](const Object& obj, std::string_view key, std::string_view path) {
				return string_at(obj, key, path);
			};

			MATHFP_TRY_LET(double, s_imp_mult, num(**search_tol, "minSearchImpFactor", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_imp_add, num(**search_tol, "minSearchImpAbs", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_jt_mult, num(**search_tol, "minJourneyTimeFactor", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_jt_add, num(**search_tol, "minJourneyTimeAbs", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_nt_mult, num(**search_tol, "minNumberTransfersFactor", "root.searchPara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, s_nt_add, num(**search_tol, "minNumberTransfersAbs", "root.searchPara.ToleranceConstraints"));

			const auto search_tolerances = make_search_tolerances(
				Dimless{ s_imp_mult }
				, Dimless{ s_imp_add }
				, Dimless{ s_jt_mult }
				, Dimless{ s_jt_add }
				, Dimless{ s_nt_mult }
				, Dimless{ s_nt_add }
			);
			if (!search_tolerances) return mathfp::unexpected(search_tolerances.error());

			MATHFP_TRY_LET(double, c_imp_mult, num(**choice_tol, "minSearchImpFactor", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_imp_add, num(**choice_tol, "minSearchImpAbs", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_jt_mult, num(**choice_tol, "minJourneyTimeFactor", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_jt_add, num(**choice_tol, "minJourneyTimeAbs", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_nt_mult, num(**choice_tol, "minNumberTransfersFactor", "root.choicePara.ToleranceConstraints"));
			MATHFP_TRY_LET(double, c_nt_add, num(**choice_tol, "minNumberTransfersAbs", "root.choicePara.ToleranceConstraints"));

			const auto choice_tolerances = make_choice_tolerances(
				Dimless{ c_imp_mult }
				, Dimless{ c_imp_add }
				, Dimless{ c_jt_mult }
				, Dimless{ c_jt_add }
				, Dimless{ c_nt_mult }
				, Dimless{ c_nt_add }
			);
			if (!choice_tolerances) return mathfp::unexpected(choice_tolerances.error());

			MATHFP_TRY_LET(double, max_transfers, num(**search_para, "maxNumTransfers", "root.searchPara"));
			MATHFP_TRY_LET(double, min_twt, num(**temporal, "minTWT", "root.searchPara.TemporalSuitability"));
			MATHFP_TRY_LET(double, max_twt, num(**temporal, "maxTWT", "root.searchPara.TemporalSuitability"));
			MATHFP_TRY_LET(double, n_transfers, num(**search_imp, "numTransfersFactor", "root.searchPara.SearchImp"));

			const auto transfer_limits = make_transfer_limits(
				TransferCount{ static_cast<std::int32_t>(max_transfers) }
				, Time{ min_twt }
				, Time{ max_twt }
				, true
				, true
			);
			if (!transfer_limits) return mathfp::unexpected(transfer_limits.error());

			MATHFP_TRY_LET(double, in_veh_factor, num(**search_imp, "inVehTimeFactor", "root.searchPara.SearchImp"));
			MATHFP_TRY_LET(double, suppl_factor, num(**search_imp, "supplementsFactor", "root.searchPara.SearchImp"));
			const auto impedance = make_search_impedance(
				Dimless{ in_veh_factor }
				, Dimless{ n_transfers }
				, Dimless{ suppl_factor }
				, Time{ n_transfers }
			);
			if (!impedance) return mathfp::unexpected(impedance.error());

			const auto preprocess = make_preprocess_params(
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
			);
			if (!preprocess) return mathfp::unexpected(preprocess.error());

			MATHFP_TRY_LET(std::string, choice_model, text(**split_para, "choiceModel", "root.splitPara"));
			double beta = 0.0;
			if (choice_model == "Kirchhoff") {
				MATHFP_TRY_LET(double, beta_v, num(**split_para, "KirchhoffExp", "root.splitPara"));
				beta = beta_v;
			} else if (choice_model == "Logit") {
				MATHFP_TRY_LET(double, beta_v, num(**split_para, "logitExp", "root.splitPara"));
				beta = beta_v;
			} else if (choice_model == "Lohse") {
				MATHFP_TRY_LET(double, beta_v, num(**split_para, "LohseExp", "root.splitPara"));
				beta = beta_v;
			} else {
				return mathfp::unexpected(
					mathfp::invalid_arg("unsupported choiceModel")
					.ctx("choiceModel", choice_model)
				);
			}

			MATHFP_TRY_LET(double, q_time, num(**split_imp, "perceivedJourneyTimeFactor", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, q_dep_early, num(**split_imp, "temporalUtilityFactor_early", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, q_dep_late, num(**split_imp, "temporalUtilityFactor_late", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, q_fare, num(**split_imp, "fareFactor", "root.splitPara.SplitImp"));
			MATHFP_TRY_LET(double, boxcox_t, num(**split_para, "BoxCoxExp", "root.splitPara"));
			MATHFP_TRY_LET(double, gamma, num(**indep, "gamma", "root.splitPara.Independence"));
			MATHFP_TRY_LET(double, x_scale, num(**indep, "indepMaxDelta", "root.splitPara.Independence"));
			MATHFP_TRY_LET(double, y_scale, num(**indep, "indepHigherQualityCoeff", "root.splitPara.Independence"));
			MATHFP_TRY_LET(double, z_scale, num(**indep, "indepLowerQualityCoeff", "root.splitPara.Independence"));
			const auto split = make_split_params(
				Dimless{ q_time }
				, Dimless{ 0.5 * (q_dep_early + q_dep_late) }
				, Dimless{ q_fare }
				, Dimless{ beta }
				, Dimless{ boxcox_t }
				, Dimless{ gamma }
				, Dimless{ x_scale }
				, Dimless{ y_scale }
				, Dimless{ z_scale }
			);
			if (!split) return mathfp::unexpected(split.error());

			return make_search_params(
				*preprocess
				, *impedance
				, *transfer_limits
				, *search_tolerances
				, *choice_tolerances
				, *split
			);
		}

	}  // namespace

	mathfp::Expected<timetable::domain::SearchParams> parse_search_params_file(
		const std::filesystem::path& path
	) {
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

		Parser parser(std::move(text));
		const auto root = parser.parse();
		if (!root) return mathfp::unexpected(root.error());

		const auto root_obj = as_object(*root, "root");
		if (!root_obj) return mathfp::unexpected(root_obj.error());

		return map_params(**root_obj);
	}

}  // namespace timetable::infra::params_txt
