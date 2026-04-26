#pragma once

#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

#include <csv.hpp>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/infra/text_parse.hpp"

namespace timetable::infra::csv_parse {

    template <class Columns>
    using ColumnDef = std::pair<std::string_view, std::size_t Columns::*>;

    template <class Columns, class Row>
    struct Int64RowFieldSpec final {
        std::string_view         csv_name{};
        std::size_t Columns::*   column_member{};
        std::int64_t Row::*      value_member{};
    };

    template <class Columns, class Row>
    struct DoubleRowFieldSpec final {
        std::string_view         csv_name{};
        std::size_t Columns::*   column_member{};
        double Row::*            value_member{};
    };

    [[nodiscard]] inline mathfp::Unexpected parse_error(
          const char*      message
        , std::size_t      row
        , std::string_view column
    ) {
        return mathfp::unexpected(
            mathfp::invalid_arg(message)
                .ctx("row", static_cast<std::int64_t>(row))
                .ctx("column", std::string(column))
        );
    }

    template <typename T, typename ParseFn>
    [[nodiscard]] inline mathfp::Expected<T> parse_numeric_cell(
          std::string_view text
        , std::size_t      row
        , std::string_view column
        , const char*      empty_message
        , const char*      invalid_message
        , const char*      range_message
        , ParseFn&&        parse
    ) {
        const auto result = text_parse::parse_numeric_token<T>(
            text, std::forward<ParseFn>(parse)
        );

        if (const auto* failure = std::get_if<text_parse::NumericParseFailure>(&result)) {
            if (*failure == text_parse::NumericParseFailure::Empty) {
                return parse_error(empty_message, row, column);
            }
            if (*failure == text_parse::NumericParseFailure::Invalid) {
                return parse_error(invalid_message, row, column);
            }
            return parse_error(range_message, row, column);
        }

        return std::get<T>(result);
    }

    [[nodiscard]] inline mathfp::Expected<std::int64_t> parse_int64_cell(
          std::string_view text
        , std::size_t      row
        , std::string_view column
    ) {
        return parse_numeric_cell<std::int64_t>(
              text
            , row
            , column
            , "empty integer field"
            , "failed to parse integer"
            , "integer out of range"
            , [](const char* begin, char** end) {
                return std::strtoll(begin, end, 10);
            }
        );
    }

    [[nodiscard]] inline mathfp::Expected<double> parse_double_cell(
          std::string_view text
        , std::size_t      row
        , std::string_view column
    ) {
        return parse_numeric_cell<double>(
              text
            , row
            , column
            , "empty floating point field"
            , "failed to parse floating point"
            , "floating point out of range"
            , [](const char* begin, char** end) {
                return std::strtod(begin, end);
            }
        );
    }

    [[nodiscard]] inline mathfp::Expected<::csv::CSVReader> open_csv(
          const std::filesystem::path& path
        , std::string_view             what
    ) {
        try {
            auto format = ::csv::CSVFormat{};
            format.delimiter(',')
                .quote('"')
                .header_row(0)
                .trim({ ' ', '\t' })
                .variable_columns(::csv::VariableColumnPolicy::THROW);

            return ::csv::CSVReader(path.string(), format);
        } catch (const std::exception& e) {
            return mathfp::unexpected(
                mathfp::invalid_arg("failed to open or initialize csv")
                    .ctx("path", path.string())
                    .ctx("kind", std::string(what))
                    .ctx("reason", std::string(e.what()))
            );
        }
    }

    template <typename Columns, typename Defs>
    [[nodiscard]] inline mathfp::Expected<Columns> resolve_csv_columns(
          const ::csv::CSVReader& reader
        , const Defs&             defs
    ) {
        Columns resolved{};
        for (const auto& [name, member] : defs) {
            const auto resolved_column = reader.index_of(std::string(name));
            if (resolved_column == ::csv::CSV_NOT_FOUND) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("missing required csv column")
                        .ctx("column", std::string(name))
                );
            }
            resolved.*member = static_cast<std::size_t>(resolved_column);
        }
        return resolved;
    }

    template <typename Columns, typename Defs>
    [[nodiscard]] inline mathfp::Expected<Columns> parse_csv_header(
          const ::csv::CSVReader&      reader
        , const std::filesystem::path& path
        , const Defs&                  defs
        , std::string_view             what
    ) {
        if (reader.get_col_names().empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("csv is empty or has no header")
                    .ctx("path", path.string())
                    .ctx("kind", std::string(what))
            );
        }

        return resolve_csv_columns<Columns>(reader, defs);
    }

    [[nodiscard]] inline std::string_view field_text(const ::csv::CSVField& field) {
        const auto sv = field.get_sv();
        return std::string_view(sv.data(), sv.size());
    }

    template <class Columns, class Row, std::size_t N>
    inline mathfp::Expected<mathfp::Unit> decode_int64_row_fields(
          Row&                                               out
        , const ::csv::CSVRow&                               row_data
        , const Columns&                                     columns
        , std::size_t                                        row
        , const std::array<Int64RowFieldSpec<Columns, Row>, N>& field_specs
    ) {
        for (const auto& spec : field_specs) {
            MATHFP_TRY_LET(
                  std::int64_t
                , value
                , parse_int64_cell(
                      field_text(row_data[columns.*(spec.column_member)])
                    , row
                    , spec.csv_name
                )
            );
            out.*(spec.value_member) = value;
        }
        return mathfp::ok();
    }

    template <class Columns, class Row, std::size_t N>
    inline mathfp::Expected<mathfp::Unit> decode_double_row_fields(
          Row&                                                out
        , const ::csv::CSVRow&                                row_data
        , const Columns&                                      columns
        , std::size_t                                         row
        , const std::array<DoubleRowFieldSpec<Columns, Row>, N>& field_specs
    ) {
        for (const auto& spec : field_specs) {
            MATHFP_TRY_LET(
                  double
                , value
                , parse_double_cell(
                      field_text(row_data[columns.*(spec.column_member)])
                    , row
                    , spec.csv_name
                )
            );
            out.*(spec.value_member) = value;
        }
        return mathfp::ok();
    }

}  // namespace timetable::infra::csv_parse
