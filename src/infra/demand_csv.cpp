#include "timetable/infra/demand_csv.hpp"

#include <array>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <csv.hpp>
#include <fmt/format.h>

#include <mathfp/core/error.hpp>
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

        mathfp::Expected<ParsedIntervalRow> parse_interval_row(
              const ::csv::CSVRow&      row_data
            , const IntervalCsvColumns& columns
            , std::size_t               row
        ) {
            MATHFP_TRY_LET(
                  std::int64_t
                , interval_id
                , csv_parse::parse_int64_cell(csv_parse::field_text(row_data[columns.interval_id]), row, "interval_id")
            );
            MATHFP_TRY_LET(
                  double
                , start_sec
                , csv_parse::parse_double_cell(csv_parse::field_text(row_data[columns.start_sec]), row, "start_sec")
            );
            MATHFP_TRY_LET(
                  double
                , end_sec
                , csv_parse::parse_double_cell(csv_parse::field_text(row_data[columns.end_sec]), row, "end_sec")
            );

            return ParsedIntervalRow{
                  .interval_id = interval_id
                , .start_sec   = start_sec
                , .end_sec     = end_sec
            };
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
            MATHFP_TRY_LET(
                  std::int64_t
                , origin_zone_id
                , csv_parse::parse_int64_cell(csv_parse::field_text(row_data[columns.origin_zone_id]), row, "origin_zone_id")
            );
            MATHFP_TRY_LET(
                  std::int64_t
                , destination_zone_id
                , csv_parse::parse_int64_cell(csv_parse::field_text(row_data[columns.destination_zone_id]), row, "destination_zone_id")
            );
            MATHFP_TRY_LET(
                  std::int64_t
                , interval_id
                , csv_parse::parse_int64_cell(csv_parse::field_text(row_data[columns.interval_id]), row, "interval_id")
            );
            MATHFP_TRY_LET(
                  double
                , passengers
                , csv_parse::parse_double_cell(csv_parse::field_text(row_data[columns.passengers]), row, "passengers")
            );

            return ParsedDemandRow{
                  .origin_zone_id      = origin_zone_id
                , .destination_zone_id = destination_zone_id
                , .interval_id         = interval_id
                , .passengers          = passengers
            };
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

}  // namespace timetable::infra::csv
