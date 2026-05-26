#include "detail/params_txt.hpp"

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>
#include <vector>

#include <tao/pegtl.hpp>

#include <mathfp/core/error.hpp>

#include "timetable/infra/text_parse.hpp"

namespace timetable::infra::params_txt::detail {

    namespace {

        namespace pegtl = tao::pegtl;

        struct ContainerState final {
            enum class Kind : std::uint8_t {
                  Object
                , Array
            };

            Kind                       kind{};
            Object                     object{};
            Array                      array{};
            std::optional<std::string> pending_key{};
        };

        struct ParserState final {
            std::vector<ContainerState> stack{};
            std::optional<Value>        root{};
            Object                      assignments{};
            std::optional<std::string>  pending_assignment{};
        };

        mathfp::Expected<ParserState> with_object_key(
              ParserState&& state
            , std::string   key
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
            , Value         value
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

            top.object     .emplace(std::move(*top.pending_key), std::move(value));
            top.pending_key.reset();
            return std::move(state);
        }

        mathfp::Expected<ParserState> with_assignment_name(
              ParserState&& state
            , std::string   name
        ) {
            if (!state.stack.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("assignment name inside container")
                );
            }
            if (state.pending_assignment.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("dangling assignment name before value")
                );
            }
            state.pending_assignment = std::move(name);
            return std::move(state);
        }

        mathfp::Expected<ParserState> with_committed_assignment(ParserState&& state) {
            if (!state.pending_assignment.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("assignment value without assignment name")
                );
            }
            if (!state.root.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("assignment without value")
                );
            }

            state.assignments.insert_or_assign(
                  std::move(*state.pending_assignment)
                , std::move(*state.root)
            );
            state.pending_assignment.reset();
            state.root.reset();
            return std::move(state);
        }

        mathfp::Expected<ParserState> with_resolved_reference(
              ParserState&& state
            , std::string   name
        ) {
            const auto it = state.assignments.find(name);
            if (it == state.assignments.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("unknown params.txt reference")
                        .ctx("reference", std::move(name))
                );
            }
            return with_pushed_value(std::move(state), it->second);
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

        double parse_numeric_product_token(std::string_view token) {
            double result = 1.0;
            std::size_t begin = 0;
            while (begin < token.size()) {
                const auto end = token.find('*', begin);
                auto part = token.substr(
                      begin
                    , end == std::string_view::npos ? std::string_view::npos : end - begin
                );

                while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front()))) {
                    part.remove_prefix(1);
                }
                while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back()))) {
                    part.remove_suffix(1);
                }
                if (part.empty()) {
                    throw std::invalid_argument("empty numeric product factor");
                }

                const std::string text(part);
                char* parsed_end = nullptr;
                errno = 0;
                const auto value = std::strtod(text.c_str(), &parsed_end);
                if (parsed_end != text.c_str() + text.size() || errno == ERANGE) {
                    throw std::invalid_argument("invalid numeric product factor");
                }
                result *= value;

                if (end == std::string_view::npos) {
                    break;
                }
                begin = end + 1;
            }
            return result;
        }

        bool starts_like_assignment_text(std::string_view text) noexcept {
            std::size_t pos = 0;
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            if (pos == text.size()) {
                return false;
            }
            const auto first = static_cast<unsigned char>(text[pos]);
            if (!std::isalpha(first) && text[pos] != '_') {
                return false;
            }
            ++pos;
            while (pos < text.size()) {
                const auto c = static_cast<unsigned char>(text[pos]);
                if (!std::isalnum(c) && text[pos] != '_') {
                    break;
                }
                ++pos;
            }
            while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
                ++pos;
            }
            return pos < text.size() && text[pos] == '=';
        }

        namespace grammar {

            struct ws : pegtl::star<pegtl::space> {};

            struct object_begin : pegtl::one<'{'> {};
            struct object_end   : pegtl::one<'}'> {};
            struct array_begin  : pegtl::one<'['> {};
            struct array_end    : pegtl::one<']'> {};
            struct comma        : pegtl::one<','> {};
            struct colon        : pegtl::one<':'> {};

            struct escaped_char       : pegtl::seq<pegtl::one<'\\'>, pegtl::any> {};
            struct single_plain_char  : pegtl::not_one<'\\', '\''> {};
            struct double_plain_char  : pegtl::not_one<'\\', '"'> {};
            struct single_string_body : pegtl::star<pegtl::sor<escaped_char, single_plain_char>> {};
            struct double_string_body : pegtl::star<pegtl::sor<escaped_char, double_plain_char>> {};
            struct single_quoted_string : pegtl::if_must<pegtl::one<'\''>, single_string_body, pegtl::one<'\''>> {};
            struct double_quoted_string : pegtl::if_must<pegtl::one<'"'>, double_string_body, pegtl::one<'"'>> {};
            struct quoted_string : pegtl::sor<single_quoted_string, double_quoted_string> {};

            struct key_string   : quoted_string {};
            struct value_string : quoted_string {};

            struct identifier_first : pegtl::sor<pegtl::alpha, pegtl::one<'_'>> {};
            struct identifier_rest  : pegtl::sor<pegtl::alnum, pegtl::one<'_'>> {};
            struct identifier       : pegtl::seq<identifier_first, pegtl::star<identifier_rest>> {};
            struct assignment_name  : identifier {};
            struct reference        : identifier {};

            struct int_part      : pegtl::plus<pegtl::digit> {};
            struct frac_part     : pegtl::seq<pegtl::one<'.'>, pegtl::star<pegtl::digit>> {};
            struct dot_frac_only : pegtl::seq<pegtl::one<'.'>, pegtl::plus<pegtl::digit>> {};

            struct number_mantissa : pegtl::sor<
                pegtl::seq<int_part, pegtl::opt<frac_part>>
                , dot_frac_only
            > {};
            struct number_exponent : pegtl::seq<
                pegtl::one<'e', 'E'>
                , pegtl::opt<pegtl::one<'+', '-'>>
                , pegtl::plus<pegtl::digit>
            > {};
            struct raw_number : pegtl::seq<
                pegtl::opt<pegtl::one<'-'>>
                , number_mantissa
                , pegtl::opt<number_exponent>
            > {};
            struct number_product : pegtl::seq<
                  raw_number
                , pegtl::plus<pegtl::seq<ws, pegtl::one<'*'>, ws, raw_number>>
            > {};
            struct number : raw_number {};

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

            struct value       : pegtl::sor<
                  object
                , array
                , value_string
                , number_product
                , number
                , kw_true
                , kw_false
                , kw_none
                , reference
            > {};
            struct value_start : pegtl::must<ws, value, ws, pegtl::eof> {};

            struct equals            : pegtl::one<'='> {};
            struct assignment_commit : pegtl::success {};
            struct assignment        : pegtl::seq<ws, assignment_name, ws, equals, ws, value, assignment_commit> {};
            struct assignment_start  : pegtl::must<ws, pegtl::plus<assignment>, ws, pegtl::eof> {};

        }  // namespace grammar

        template <typename Rule>
        struct action final : pegtl::nothing<Rule> {};

        template <>
        struct action<grammar::assignment_name> final {
            template <typename Input>
            static void apply(const Input& in, ParserState& state) {
                apply_parser_transition(
                      state
                    , in
                    , [&](ParserState current) {
                        return with_assignment_name(std::move(current), in.string());
                    }
                );
            }
        };

        template <>
        struct action<grammar::assignment_commit> final {
            template <typename Input>
            static void apply(const Input& in, ParserState& state) {
                apply_parser_transition(state, in, with_committed_assignment);
            }
        };

        template <>
        struct action<grammar::reference> final {
            template <typename Input>
            static void apply(const Input& in, ParserState& state) {
                apply_parser_transition(
                      state
                    , in
                    , [&](ParserState current) {
                        return with_resolved_reference(std::move(current), in.string());
                    }
                );
            }
        };

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
        struct action<grammar::number_product> final {
            template <typename Input>
            static void apply(const Input& in, ParserState& state) {
                try {
                    apply_parser_transition(
                          state
                        , in
                        , [&](ParserState current) {
                            return with_pushed_value(
                                  std::move(current)
                                , Value{ parse_numeric_product_token(in.string_view()) }
                            );
                        }
                    );
                } catch (const std::invalid_argument& e) {
                    throw pegtl::parse_error(e.what(), in);
                }
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

    }  // namespace

    mathfp::Expected<Value> parse_value_text(
          const std::string& text
        , std::string_view   source_name
    ) {
        const auto parse_assignment_text = [&]() -> mathfp::Expected<Value> {
            pegtl::memory_input in(text, source_name);
            ParserState state;
            pegtl::parse<grammar::assignment_start, action>(in, state);
            if (!state.stack.empty() || state.pending_assignment.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("incomplete params.txt assignment parse state")
                        .ctx("source", std::string(source_name))
                );
            }

            const auto py_para = state.assignments.find("pyPara");
            if (py_para != state.assignments.end()) {
                return py_para->second;
            }
            if (state.assignments.size() == 1) {
                return state.assignments.begin()->second;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("params.txt assignment file does not define pyPara")
                    .ctx("source", std::string(source_name))
            );
        };

        const auto parse_value = [&]() -> mathfp::Expected<Value> {
            pegtl::memory_input in(text, source_name);
            ParserState state;
            pegtl::parse<grammar::value_start, action>(in, state);
            if (!state.stack.empty() || !state.root.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("incomplete parse state")
                        .ctx("source", std::string(source_name))
                );
            }
            return std::move(*state.root);
        };

        try {
            if (starts_like_assignment_text(text)) {
                return parse_assignment_text();
            }
            return parse_value();
        } catch (const pegtl::parse_error& e) {
            if (!starts_like_assignment_text(text)) {
                try {
                    return parse_assignment_text();
                } catch (const pegtl::parse_error&) {
                    // Report the original value-parse error; it points at the
                    // first construct that made the file non-object-like.
                }
            }

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

}  // namespace timetable::infra::params_txt::detail
