#include "timetable/infra/demand_csv.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <csv.hpp>
#include <fmt/format.h>
#include <xlsxio_read.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/infra/csv_parse.hpp"
#include "timetable/domain/scalars.hpp"
#include "timetable/domain/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::csv {

    namespace {
        using timetable::domain::DemandEntry;
        using timetable::domain::IntervalId;
        using timetable::domain::Time;
        using timetable::domain::TimeInterval;
        using timetable::domain::ZoneId;

        struct IntervalCsvColumns final {
            std::size_t interval_id{};
            std::size_t start_sec{};
            std::size_t end_sec{};
        };

        struct DemandCsvColumns final {
            std::size_t origin_zone_id{};
            std::size_t destination_zone_id{};
            std::size_t interval_id{};
            std::size_t passengers{};
        };

        struct ParsedIntervalRow final {
            std::int64_t interval_id{};
            double       start_sec{};
            double       end_sec{};
        };

        struct ParsedDemandRow final {
            std::int64_t origin_zone_id{};
            std::int64_t destination_zone_id{};
            std::int64_t interval_id{};
            double       passengers{};
        };

        struct XlsxReadDeleter final {
            void operator()(xlsxioread handle) const noexcept {
                if (handle != nullptr) {
                    xlsxioread_close(handle);
                }
            }
        };

        struct XlsxSheetDeleter final {
            void operator()(xlsxioreadersheet sheet) const noexcept {
                if (sheet != nullptr) {
                    xlsxioread_sheet_close(sheet);
                }
            }
        };

        struct XlsxCellDeleter final {
            void operator()(char* value) const noexcept {
                if (value != nullptr) {
                    xlsxioread_free(value);
                }
            }
        };

        using XlsxReadHandle = std::unique_ptr<std::remove_pointer_t<xlsxioread>, XlsxReadDeleter>;
        using XlsxSheetHandle =
            std::unique_ptr<std::remove_pointer_t<xlsxioreadersheet>, XlsxSheetDeleter>;
        using XlsxCellValue = std::unique_ptr<char, XlsxCellDeleter>;

        constexpr auto interval_int_row_field_specs() {
            return std::array<csv_parse::Int64RowFieldSpec<IntervalCsvColumns, ParsedIntervalRow>, 1>{
                csv_parse::Int64RowFieldSpec<IntervalCsvColumns, ParsedIntervalRow>{
                    "interval_id", &IntervalCsvColumns::interval_id, &ParsedIntervalRow::interval_id
                }
            };
        }

        constexpr auto interval_double_row_field_specs() {
            return std::array<csv_parse::DoubleRowFieldSpec<IntervalCsvColumns, ParsedIntervalRow>, 2>{
                  csv_parse::DoubleRowFieldSpec<IntervalCsvColumns, ParsedIntervalRow>{
                      "start_sec", &IntervalCsvColumns::start_sec, &ParsedIntervalRow::start_sec
                  }
                , csv_parse::DoubleRowFieldSpec<IntervalCsvColumns, ParsedIntervalRow>{
                      "end_sec", &IntervalCsvColumns::end_sec, &ParsedIntervalRow::end_sec
                  }
            };
        }

        constexpr auto demand_int_row_field_specs() {
            return std::array<csv_parse::Int64RowFieldSpec<DemandCsvColumns, ParsedDemandRow>, 3>{
                  csv_parse::Int64RowFieldSpec<DemandCsvColumns, ParsedDemandRow>{
                      "origin_zone_id", &DemandCsvColumns::origin_zone_id, &ParsedDemandRow::origin_zone_id
                  }
                , csv_parse::Int64RowFieldSpec<DemandCsvColumns, ParsedDemandRow>{
                      "destination_zone_id", &DemandCsvColumns::destination_zone_id, &ParsedDemandRow::destination_zone_id
                  }
                , csv_parse::Int64RowFieldSpec<DemandCsvColumns, ParsedDemandRow>{
                      "interval_id", &DemandCsvColumns::interval_id, &ParsedDemandRow::interval_id
                  }
            };
        }

        constexpr auto demand_double_row_field_specs() {
            return std::array<csv_parse::DoubleRowFieldSpec<DemandCsvColumns, ParsedDemandRow>, 1>{
                csv_parse::DoubleRowFieldSpec<DemandCsvColumns, ParsedDemandRow>{
                    "passengers", &DemandCsvColumns::passengers, &ParsedDemandRow::passengers
                }
            };
        }

        constexpr auto interval_csv_column_defs() {
            return std::array<csv_parse::ColumnDef<IntervalCsvColumns>, 3>{
                  csv_parse::ColumnDef<IntervalCsvColumns>{ "interval_id", &IntervalCsvColumns::interval_id }
                , csv_parse::ColumnDef<IntervalCsvColumns>{ "start_sec"  , &IntervalCsvColumns::start_sec }
                , csv_parse::ColumnDef<IntervalCsvColumns>{ "end_sec"    , &IntervalCsvColumns::end_sec }
            };
        }

        constexpr auto demand_csv_column_defs() {
            return std::array<csv_parse::ColumnDef<DemandCsvColumns>, 4>{
                  csv_parse::ColumnDef<DemandCsvColumns>{ "origin_zone_id"     , &DemandCsvColumns::origin_zone_id }
                , csv_parse::ColumnDef<DemandCsvColumns>{ "destination_zone_id", &DemandCsvColumns::destination_zone_id }
                , csv_parse::ColumnDef<DemandCsvColumns>{ "interval_id"        , &DemandCsvColumns::interval_id }
                , csv_parse::ColumnDef<DemandCsvColumns>{ "passengers"         , &DemandCsvColumns::passengers }
            };
        }

        [[nodiscard]] std::string_view trim_ascii(std::string_view value) noexcept {
            while (!value.empty()
                && (value.front() == ' ' || value.front() == '\t'
                    || value.front() == '\r' || value.front() == '\n')) {
                value.remove_prefix(1);
            }
            while (!value.empty()
                && (value.back() == ' ' || value.back() == '\t'
                    || value.back() == '\r' || value.back() == '\n')) {
                value.remove_suffix(1);
            }
            return value;
        }

        [[nodiscard]] mathfp::Expected<double> parse_xlsx_number(
              std::string_view value
            , std::string_view field
            , std::size_t      row
            , std::size_t      column
        ) {
            const auto trimmed = trim_ascii(value);
            if (trimmed.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("xlsx numeric cell is empty")
                        .ctx("field", std::string(field))
                        .ctx("row", static_cast<std::int64_t>(row))
                        .ctx("column", static_cast<std::int64_t>(column))
                );
            }

            std::string normalized{ trimmed };
            for (char& ch : normalized) {
                if (ch == ',') {
                    ch = '.';
                }
            }

            char* end = nullptr;
            const double parsed = std::strtod(normalized.c_str(), &end);
            if (end == normalized.c_str() || *end != '\0' || !std::isfinite(parsed)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("xlsx numeric cell is invalid")
                        .ctx("field", std::string(field))
                        .ctx("row", static_cast<std::int64_t>(row))
                        .ctx("column", static_cast<std::int64_t>(column))
                        .ctx("value", std::string(value))
                );
            }
            return parsed;
        }

        [[nodiscard]] mathfp::Expected<std::int64_t> parse_xlsx_integer(
              std::string_view value
            , std::string_view field
            , std::size_t      row
            , std::size_t      column
        ) {
            MATHFP_TRY_LET(double, parsed, parse_xlsx_number(value, field, row, column));
            const auto rounded = std::llround(parsed);
            if (!mathfp::almost_equal(parsed, static_cast<double>(rounded))) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("xlsx integer cell has fractional value")
                        .ctx("field", std::string(field))
                        .ctx("row", static_cast<std::int64_t>(row))
                        .ctx("column", static_cast<std::int64_t>(column))
                        .ctx("value", std::string(value))
                );
            }
            return rounded;
        }

        mathfp::Expected<ParsedIntervalRow> parse_interval_row(
              const ::csv::CSVRow&      row_data
            , const IntervalCsvColumns& columns
            , std::size_t               row
        ) {
            ParsedIntervalRow parsed_row{};
            MATHFP_TRY(csv_parse::decode_int64_row_fields(
                  parsed_row
                , row_data
                , columns
                , row
                , interval_int_row_field_specs()
            ));
            MATHFP_TRY(csv_parse::decode_double_row_fields(
                  parsed_row
                , row_data
                , columns
                , row
                , interval_double_row_field_specs()
            ));
            return parsed_row;
        }

        mathfp::Expected<TimeInterval> build_time_interval(
              const ParsedIntervalRow& row
            , std::size_t              csv_row
        ) {
            const auto interval = TimeInterval{
                  .id    = IntervalId{ row.interval_id }
                , .start = Time{ row.start_sec }
                , .end   = Time{ row.end_sec }
            };

            MATHFP_TRY(timetable::domain::validation::ensure_nonneg(interval.start, "time_intervals.start_sec"));
            MATHFP_TRY(timetable::domain::validation::ensure_nonneg(interval.end, "time_intervals.end_sec"));

            if (!(interval.start.value() < interval.end.value())) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("time interval must satisfy start < end")
                        .ctx("row", static_cast<std::int64_t>(csv_row))
                        .ctx("interval_id", interval.id.get())
                        .ctx("start_sec", interval.start.value())
                        .ctx("end_sec", interval.end.value())
                );
            }

            return interval;
        }

        mathfp::Expected<ParsedDemandRow> parse_demand_row(
              const ::csv::CSVRow&    row_data
            , const DemandCsvColumns& columns
            , std::size_t             row
        ) {
            ParsedDemandRow parsed_row{};
            MATHFP_TRY(csv_parse::decode_int64_row_fields(
                  parsed_row
                , row_data
                , columns
                , row
                , demand_int_row_field_specs()
            ));
            MATHFP_TRY(csv_parse::decode_double_row_fields(
                  parsed_row
                , row_data
                , columns
                , row
                , demand_double_row_field_specs()
            ));
            return parsed_row;
        }

        mathfp::Expected<DemandEntry> build_demand_entry(
              const ParsedDemandRow&                      row
            , std::size_t                                 csv_row
            , const std::map<IntervalId, const TimeInterval*>& intervals
        ) {
            const auto interval_id = IntervalId{ row.interval_id };
            if (!intervals.contains(interval_id)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("od demand references unknown interval")
                        .ctx("row", static_cast<std::int64_t>(csv_row))
                        .ctx("interval_id", row.interval_id)
                );
            }

            if (!std::isfinite(row.passengers) || row.passengers < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("od demand passengers must be finite and non-negative")
                        .ctx("row", static_cast<std::int64_t>(csv_row))
                        .ctx("origin_zone_id", row.origin_zone_id)
                        .ctx("destination_zone_id", row.destination_zone_id)
                        .ctx("interval_id", row.interval_id)
                        .ctx("passengers", row.passengers)
                );
            }

            return DemandEntry{
                  .origin      = ZoneId{ row.origin_zone_id }
                , .destination = ZoneId{ row.destination_zone_id }
                , .interval    = interval_id
                , .passengers  = row.passengers
            };
        }

        std::map<IntervalId, const TimeInterval*> build_interval_map(
            const std::vector<TimeInterval>& intervals
        ) {
            std::map<IntervalId, const TimeInterval*> out;
            for (const auto& interval : intervals) {
                out.emplace(interval.id, &interval);
            }
            return out;
        }

        template <typename T, typename ParsedRow, typename Columns, typename ParseRow, typename BuildValue>
        mathfp::Expected<std::vector<T>> parse_csv_rows(
              ::csv::CSVReader&            reader
            , const Columns&               columns
            , const std::filesystem::path& path
            , std::string_view             what
            , ParseRow&&                   parse_row
            , BuildValue&&                 build_value
        ) {
            std::vector<T> out;
            ::csv::CSVRow row_data;
            std::size_t row = 1;

            try {
                while (reader.read_row(row_data)) {
                    ++row;
                    MATHFP_TRY_LET(ParsedRow, parsed_row, parse_row(row_data, columns, row));
                    MATHFP_TRY_LET(T, value, build_value(parsed_row, row));
                    out.push_back(std::move(value));
                }
            } catch (const std::exception& e) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed while reading csv rows")
                        .ctx("path", path.string())
                        .ctx("kind", std::string(what))
                        .ctx("row", static_cast<std::int64_t>(row))
                        .ctx("reason", std::string(e.what()))
                );
            }

            if (out.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("csv contains no data rows")
                        .ctx("path", path.string())
                        .ctx("kind", std::string(what))
                );
            }

            return out;
        }

    }  // namespace

    mathfp::Expected<std::vector<TimeInterval>> parse_time_intervals_csv(
        const std::filesystem::path& path
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        status("parsing: opening time intervals csv");
        auto reader_result = csv_parse::open_csv(path, "time_intervals.csv");
        if (!reader_result) {
            return mathfp::unexpected(std::move(reader_result.error()));
        }
        auto reader = std::move(*reader_result);
        status("parsing: reading time intervals csv header");
        auto columns_result = csv_parse::parse_csv_header<IntervalCsvColumns>(
              reader
            , path
            , interval_csv_column_defs()
            , "time_intervals.csv"
        );
        if (!columns_result) {
            return mathfp::unexpected(std::move(columns_result.error()));
        }
        auto columns = std::move(*columns_result);

        auto intervals_result = parse_csv_rows<TimeInterval, ParsedIntervalRow>(
              reader
            , columns
            , path
            , "time_intervals.csv"
            , parse_interval_row
            , [](const ParsedIntervalRow& row, std::size_t csv_row) {
                return build_time_interval(row, csv_row);
            }
        );
        if (!intervals_result) {
            return mathfp::unexpected(std::move(intervals_result.error()));
        }
        auto intervals = std::move(*intervals_result);

        log(
            fmt::format(
                  "parsing: time intervals csv parsed; intervals = {}"
                , intervals.size()
            )
            , LogLevel::Info
        );
        status("parsing: time intervals csv parsed");
        return intervals;
    }

    mathfp::Expected<std::vector<DemandEntry>> parse_od_demand_csv(
          const std::filesystem::path&  path
        , const std::vector<TimeInterval>& intervals
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        status("parsing: opening od demand csv");
        auto reader_result = csv_parse::open_csv(path, "od_demand.csv");
        if (!reader_result) {
            return mathfp::unexpected(std::move(reader_result.error()));
        }
        auto reader = std::move(*reader_result);
        status("parsing: reading od demand csv header");
        auto columns_result = csv_parse::parse_csv_header<DemandCsvColumns>(
              reader
            , path
            , demand_csv_column_defs()
            , "od_demand.csv"
        );
        if (!columns_result) {
            return mathfp::unexpected(std::move(columns_result.error()));
        }
        auto columns = std::move(*columns_result);

        const auto interval_map = build_interval_map(intervals);
        auto demand_result = parse_csv_rows<DemandEntry, ParsedDemandRow>(
              reader
            , columns
            , path
            , "od_demand.csv"
            , parse_demand_row
            , [&](const ParsedDemandRow& row, std::size_t csv_row) {
                return build_demand_entry(row, csv_row, interval_map);
            }
        );
        if (!demand_result) {
            return mathfp::unexpected(std::move(demand_result.error()));
        }
        auto demand = std::move(*demand_result);

        log(
            fmt::format(
                  "parsing: od demand csv parsed; entries = {}"
                , demand.size()
            )
            , LogLevel::Info
        );
        status("parsing: od demand csv parsed");
        return demand;
    }

    mathfp::Expected<std::vector<DemandEntry>> parse_daily_od_matrix_xlsx(
          const std::filesystem::path& path
        , IntervalId                   interval
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        status("parsing: opening daily OD matrix xlsx");
        XlsxReadHandle workbook{ xlsxioread_open(path.string().c_str()) };
        if (workbook == nullptr) {
            return mathfp::unexpected(
                mathfp::invalid_arg("failed to open daily OD matrix xlsx")
                    .ctx("path", path.string())
            );
        }

        XlsxSheetHandle sheet{
            xlsxioread_sheet_open(workbook.get(), nullptr, 0u)
        };
        if (sheet == nullptr) {
            return mathfp::unexpected(
                mathfp::invalid_arg("failed to open first worksheet in daily OD matrix xlsx")
                    .ctx("path", path.string())
            );
        }

        std::vector<ZoneId> destination_ids;
        std::vector<ZoneId> row_origin_ids;
        std::vector<DemandEntry> demand;
        std::size_t row_index = 0;
        double total_passengers = 0.0;

        status("parsing: reading daily OD matrix xlsx");
        while (xlsxioread_sheet_next_row(sheet.get()) != 0) {
            ++row_index;
            std::vector<std::string> cells;

            while (true) {
                XlsxCellValue raw_cell{
                    xlsxioread_sheet_next_cell(sheet.get())
                };
                if (raw_cell == nullptr) {
                    break;
                }
                cells.emplace_back(raw_cell.get());
            }

            if (row_index == 1) {
                if (cells.size() < 4u) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("daily OD matrix xlsx header row is too short")
                            .ctx("path", path.string())
                            .ctx("row", static_cast<std::int64_t>(row_index))
                    );
                }
                destination_ids.reserve(cells.size() - 3u);
                for (std::size_t column = 4; column <= cells.size(); ++column) {
                    MATHFP_TRY_LET(
                          std::int64_t
                        , destination
                        , parse_xlsx_integer(
                              cells[column - 1u]
                            , "destination_zone_id"
                            , row_index
                            , column
                          )
                    );
                    if (destination <= 0) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("daily OD matrix destination zone id must be positive")
                                .ctx("path", path.string())
                                .ctx("column", static_cast<std::int64_t>(column))
                                .ctx("destination_zone_id", destination)
                        );
                    }
                    destination_ids.push_back(ZoneId{ destination });
                }
                continue;
            }

            if (row_index <= 3u) {
                continue;
            }

            if (cells.empty()) {
                continue;
            }
            if (destination_ids.empty()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("daily OD matrix xlsx has no destination header")
                        .ctx("path", path.string())
                );
            }
            if (cells.size() < destination_ids.size() + 3u) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("daily OD matrix xlsx row is shorter than destination header")
                        .ctx("path", path.string())
                        .ctx("row", static_cast<std::int64_t>(row_index))
                        .ctx("cells", static_cast<std::int64_t>(cells.size()))
                        .ctx("expected_min_cells", static_cast<std::int64_t>(destination_ids.size() + 3u))
                );
            }

            MATHFP_TRY_LET(
                  std::int64_t
                , origin
                , parse_xlsx_integer(cells[0], "origin_zone_id", row_index, 1u)
            );
            if (origin <= 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("daily OD matrix origin zone id must be positive")
                        .ctx("path", path.string())
                        .ctx("row", static_cast<std::int64_t>(row_index))
                        .ctx("origin_zone_id", origin)
                );
            }
            row_origin_ids.push_back(ZoneId{ origin });

            for (std::size_t target_index = 0; target_index < destination_ids.size(); ++target_index) {
                const auto column = target_index + 4u;
                MATHFP_TRY_LET(
                      double
                    , passengers
                    , parse_xlsx_number(
                          cells[column - 1u]
                        , "passengers"
                        , row_index
                        , column
                      )
                );
                if (passengers < 0.0) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("daily OD matrix passengers must be non-negative")
                            .ctx("path", path.string())
                            .ctx("row", static_cast<std::int64_t>(row_index))
                            .ctx("column", static_cast<std::int64_t>(column))
                            .ctx("passengers", passengers)
                    );
                }
                if (mathfp::almost_zero(passengers)) {
                    continue;
                }

                total_passengers += passengers;
                demand.push_back(
                    DemandEntry{
                          .origin      = ZoneId{ origin }
                        , .destination = destination_ids[target_index]
                        , .interval    = interval
                        , .passengers  = passengers
                    }
                );
            }
        }

        if (destination_ids.empty() || row_origin_ids.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("daily OD matrix xlsx contains no OD matrix data")
                    .ctx("path", path.string())
            );
        }
        if (destination_ids.size() != row_origin_ids.size()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("daily OD matrix xlsx must be square")
                    .ctx("path", path.string())
                    .ctx("origins", static_cast<std::int64_t>(row_origin_ids.size()))
                    .ctx("destinations", static_cast<std::int64_t>(destination_ids.size()))
            );
        }
        for (std::size_t i = 0; i < destination_ids.size(); ++i) {
            if (destination_ids[i] != row_origin_ids[i]) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("daily OD matrix xlsx origin and destination zone axes differ")
                        .ctx("path", path.string())
                        .ctx("position", static_cast<std::int64_t>(i + 1u))
                        .ctx("origin_zone_id", row_origin_ids[i].get())
                        .ctx("destination_zone_id", destination_ids[i].get())
                );
            }
        }
        if (demand.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("daily OD matrix xlsx contains no positive demand")
                    .ctx("path", path.string())
            );
        }

        log(
            fmt::format(
                  "parsing: daily OD matrix xlsx parsed; zones = {}  cells = {}  positive_entries = {}  interval = {}  total_passengers = {:.6f}"
                , destination_ids.size()
                , destination_ids.size() * row_origin_ids.size()
                , demand.size()
                , interval.get()
                , total_passengers
            )
            , LogLevel::Info
        );
        status("parsing: daily OD matrix xlsx parsed");
        return demand;
    }

}  // namespace timetable::infra::csv
