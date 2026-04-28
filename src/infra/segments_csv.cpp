#include "timetable/infra/segments_csv.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <csv.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/infra/csv_parse.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::csv {

    namespace {
        constexpr std::size_t kRowProgressStep = 100'000;

        struct SegmentCsvColumns final {
            std::size_t from_stop{};
            std::size_t to_stop{};
            std::size_t time{};
            std::size_t length{};
            std::size_t from_zone{};
            std::size_t to_zone{};
            std::size_t fare{};
            std::size_t trip{};
            std::optional<std::size_t> route{};
            std::size_t line{};
            std::size_t from_index{};
            std::size_t dep{};
            std::size_t to_index{};
            std::size_t arr{};
        };

        struct ParsedSegmentRow final {
            std::int64_t from_zone{};
            std::int64_t from_stop{};
            std::int64_t to_zone{};
            std::int64_t to_stop{};
            std::int64_t trip_id{};
            std::int64_t route_id{ -1 };
            std::int64_t line_id{};
            std::int64_t from_index{};
            std::int64_t to_index{};
            double       length{};
            double       time{};
            double       dep{};
            double       arr{};
            double       fare{};
        };

        struct ParsedCsvHeader final {
            SegmentCsvColumns columns{};
            std::size_t       header_column_count{};
        };

        struct ParsedCsvData final {
            SegmentColumns                   columns{};
            std::unordered_set<std::int64_t> zone_set{};
        };

        constexpr auto segment_csv_column_defs() {
            return std::array<csv_parse::ColumnDef<SegmentCsvColumns>, 13>{
                  csv_parse::ColumnDef<SegmentCsvColumns>{ "FROM_STOP_ID", &SegmentCsvColumns::from_stop }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "TO_STOP_ID", &SegmentCsvColumns::to_stop }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "TIME", &SegmentCsvColumns::time }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "LENGTH", &SegmentCsvColumns::length }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "FROM_ZONE_ID", &SegmentCsvColumns::from_zone }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "TO_ZONE_ID", &SegmentCsvColumns::to_zone }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "FARE", &SegmentCsvColumns::fare }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "TRIP_ID", &SegmentCsvColumns::trip }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "LINE_ID", &SegmentCsvColumns::line }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "FROM_INDEX", &SegmentCsvColumns::from_index }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "DEP", &SegmentCsvColumns::dep }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "TO_INDEX", &SegmentCsvColumns::to_index }
                , csv_parse::ColumnDef<SegmentCsvColumns>{ "ARR", &SegmentCsvColumns::arr }
            };
        }

        constexpr auto int_row_field_specs() {
            return std::array<csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>, 8>{
                  csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "FROM_ZONE_ID", &SegmentCsvColumns::from_zone, &ParsedSegmentRow::from_zone
                  }
                , csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "FROM_STOP_ID", &SegmentCsvColumns::from_stop, &ParsedSegmentRow::from_stop
                  }
                , csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "TO_ZONE_ID", &SegmentCsvColumns::to_zone, &ParsedSegmentRow::to_zone
                  }
                , csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "TO_STOP_ID", &SegmentCsvColumns::to_stop, &ParsedSegmentRow::to_stop
                  }
                , csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "TRIP_ID", &SegmentCsvColumns::trip, &ParsedSegmentRow::trip_id
                  }
                , csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "LINE_ID", &SegmentCsvColumns::line, &ParsedSegmentRow::line_id
                  }
                , csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "FROM_INDEX", &SegmentCsvColumns::from_index, &ParsedSegmentRow::from_index
                  }
                , csv_parse::Int64RowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "TO_INDEX", &SegmentCsvColumns::to_index, &ParsedSegmentRow::to_index
                  }
            };
        }

        constexpr auto double_row_field_specs() {
            return std::array<csv_parse::DoubleRowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>, 5>{
                  csv_parse::DoubleRowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "LENGTH", &SegmentCsvColumns::length, &ParsedSegmentRow::length
                  }
                , csv_parse::DoubleRowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "TIME", &SegmentCsvColumns::time, &ParsedSegmentRow::time
                  }
                , csv_parse::DoubleRowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "DEP", &SegmentCsvColumns::dep, &ParsedSegmentRow::dep
                  }
                , csv_parse::DoubleRowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "ARR", &SegmentCsvColumns::arr, &ParsedSegmentRow::arr
                  }
                , csv_parse::DoubleRowFieldSpec<SegmentCsvColumns, ParsedSegmentRow>{
                      "FARE", &SegmentCsvColumns::fare, &ParsedSegmentRow::fare
                  }
            };
        }

        mathfp::Expected<::csv::CSVReader> open_connection_segments_csv(
            const std::filesystem::path& path
        ) {
            return csv_parse::open_csv(path, "connection_segments.csv");
        }

        mathfp::Expected<ParsedCsvHeader> parse_csv_header(
              const ::csv::CSVReader&      reader
            , const std::filesystem::path& path
        ) {
            const auto header = reader.get_col_names();
            if (header.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segments csv is empty or has no header")
                        .ctx("path", path.string())
                );
            }

            MATHFP_TRY_LET(
                  SegmentCsvColumns
                , columns
                , csv_parse::resolve_csv_columns<SegmentCsvColumns>(
                      reader
                    , segment_csv_column_defs()
                )
            );
            const auto route_column = reader.index_of("ROUTE_ID");
            if (route_column != ::csv::CSV_NOT_FOUND) {
                columns.route = static_cast<std::size_t>(route_column);
            }

            return ParsedCsvHeader{
                  .columns             = columns
                , .header_column_count = header.size()
            };
        }

        mathfp::Expected<mathfp::Unit> decode_int_row_fields(
              ParsedSegmentRow&        out
            , const ::csv::CSVRow&     row_data
            , const SegmentCsvColumns& cols
            , std::size_t              row
        ) {
            return csv_parse::decode_int64_row_fields(
                  out
                , row_data
                , cols
                , row
                , int_row_field_specs()
            );
        }

        mathfp::Expected<mathfp::Unit> decode_double_row_fields(
              ParsedSegmentRow&        out
            , const ::csv::CSVRow&     row_data
            , const SegmentCsvColumns& cols
            , std::size_t              row
        ) {
            return csv_parse::decode_double_row_fields(
                  out
                , row_data
                , cols
                , row
                , double_row_field_specs()
            );
        }

        mathfp::Expected<ParsedSegmentRow> parse_segment_row(
              const ::csv::CSVRow&     row_data
            , const SegmentCsvColumns& cols
            , std::size_t              row
        ) {
            ParsedSegmentRow parsed_row{};
            MATHFP_TRY(decode_int_row_fields(parsed_row, row_data, cols, row));
            if (cols.route.has_value()) {
                MATHFP_TRY_LET(
                      std::int64_t
                    , route_id
                    , csv_parse::parse_int64_cell(
                          csv_parse::field_text(row_data[*cols.route])
                        , row
                        , "ROUTE_ID"
                    )
                );
                parsed_row.route_id = route_id;
            }
            MATHFP_TRY(decode_double_row_fields(parsed_row, row_data, cols, row));
            return parsed_row;
        }

        void append_segment_row(
              SegmentColumns&         out
            , const ParsedSegmentRow& row
            , bool                    include_route_id
        ) {
            out.from_zone_id.push_back(row.from_zone);
            out.from_stop_id.push_back(row.from_stop);
            out.to_zone_id  .push_back(row.to_zone);
            out.to_stop_id  .push_back(row.to_stop);
            out.profile_id  .push_back(row.line_id);
            out.trip_id     .push_back(row.trip_id);
            if (include_route_id) {
                out.route_id.push_back(row.route_id);
            }
            out.from_index  .push_back(row.from_index);
            out.to_index    .push_back(row.to_index);
            out.length_km   .push_back(row.length);
            out.time_sec    .push_back(row.time);
            out.dep_sec     .push_back(row.dep);
            out.arr_sec     .push_back(row.arr);
            out.fare        .push_back(row.fare);
        }

        void collect_row_zones(
              std::unordered_set<std::int64_t>& zone_set
            , const ParsedSegmentRow&           row
        ) {
            if (row.from_zone >= 0) zone_set.insert(row.from_zone);
            if (row.to_zone >= 0) zone_set.insert(row.to_zone);
        }

        void finalize_zone_ids(
              SegmentColumns&                         out
            , const std::unordered_set<std::int64_t>& zone_set
        ) {
            out.zone_ids.assign(zone_set.begin(), zone_set.end());
            std::sort(out.zone_ids.begin(), out.zone_ids.end());
        }

        void report_csv_row_progress(
              std::size_t row
            , std::size_t segment_count
            , std::size_t zone_count
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

            if ((row % kRowProgressStep) != 0) {
                return;
            }

            status(fmt::format("parsing: reading connection segments csv (row {})", row));
            log(
                fmt::format(
                      "parsing: csv progress row={}  segments={}  zones={}"
                    , row
                    , segment_count
                    , zone_count
                )
                , LogLevel::Debug
            );
        }

        mathfp::Expected<ParsedCsvData> parse_csv_data_rows(
              ::csv::CSVReader&        reader
            , const SegmentCsvColumns& cols
        ) {
            ParsedCsvData data;
            data.zone_set.reserve(256);

            std::size_t row = 1;
            ::csv::CSVRow row_data;
            timetable::infra::progress::status("parsing: reading connection segments csv (0 rows)");

            try {
                while (reader.read_row(row_data)) {
                    ++row;
                    report_csv_row_progress(
                          row
                        , data.columns.from_zone_id.size()
                        , data.zone_set.size()
                    );

                    MATHFP_TRY_LET(ParsedSegmentRow, parsed_row, parse_segment_row(row_data, cols, row));
                    append_segment_row(data.columns, parsed_row, cols.route.has_value());
                    collect_row_zones(data.zone_set, parsed_row);
                }
            } catch (const std::exception& e) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed while reading connection segments csv rows")
                        .ctx("row"   , static_cast<std::int64_t>(row))
                        .ctx("reason", std::string(e.what()))
                );
            }

            return data;
        }

        mathfp::Expected<SegmentColumns> finalize_parsed_csv_data(
              ParsedCsvData                data
            , const std::filesystem::path& path
        ) {
            finalize_zone_ids(data.columns, data.zone_set);

            if (data.columns.from_zone_id.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("connection segments csv contains no data rows")
                    .ctx("path", path.string())
                );
            }

            return data.columns;
        }

    }  // namespace

    mathfp::Expected<SegmentColumns> parse_connection_segments_csv(
        const std::filesystem::path& path
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        status("parsing: opening connection segments csv");
        auto input_result = open_connection_segments_csv(path);
        if (!input_result) {
            return mathfp::unexpected(input_result.error());
        }
        auto input = std::move(*input_result);
        status("parsing: reading connection segments csv header");
        MATHFP_TRY_LET(ParsedCsvHeader, header, parse_csv_header(input, path));
        log(
              fmt::format("parsing: csv header loaded from {}", path.string())
            , LogLevel::Info
        );

        log(
            fmt::format(
                  "parsing: csv columns resolved; header_columns = {}"
                , header.header_column_count
            )
            , LogLevel::Info
        );

        MATHFP_TRY_LET(ParsedCsvData, parsed_data, parse_csv_data_rows(input, header.columns));
        MATHFP_TRY_LET(SegmentColumns, out, finalize_parsed_csv_data(std::move(parsed_data), path));
        log(
            fmt::format(
                  "parsing: csv parsed; segments = {}  zones = {}"
                , out.from_zone_id.size()
                , out.zone_ids.size()
            )
            , LogLevel::Info
        );
        status("parsing: connection segments csv parsed");

        return out;
    }

}  // namespace timetable::infra::csv
