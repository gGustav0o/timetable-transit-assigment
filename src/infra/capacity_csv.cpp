#include "timetable/infra/capacity_csv.hpp"

#include <array>
#include <cstdint>
#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <csv.hpp>
#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/infra/csv_parse.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::csv {
    namespace {

        using timetable::domain::RoutePosition;
        using timetable::domain::TripId;
        using timetable::domain::assignment::VehicleJourneyItemCapacity;
        using timetable::domain::assignment::VehicleJourneyItemCapacitySet;
        using timetable::domain::assignment::VehicleJourneyItemKey;

        struct CapacityCsvColumns final {
            std::size_t vehicle_journey_id{};
            std::size_t index{};
            std::size_t total_capacity{};
            std::size_t seat_capacity{};
        };

        struct ParsedCapacityRow final {
            std::int64_t vehicle_journey_id{};
            std::int64_t index{};
            double       total_capacity{};
            double       seat_capacity{};
        };

        constexpr auto capacity_csv_column_defs() {
            return std::array<csv_parse::ColumnDef<CapacityCsvColumns>, 4>{
                  csv_parse::ColumnDef<CapacityCsvColumns>{ "_VEHJOURNEYID", &CapacityCsvColumns::vehicle_journey_id }
                , csv_parse::ColumnDef<CapacityCsvColumns>{ "INDEX"        , &CapacityCsvColumns::index }
                , csv_parse::ColumnDef<CapacityCsvColumns>{ "TOTALCAP"     , &CapacityCsvColumns::total_capacity }
                , csv_parse::ColumnDef<CapacityCsvColumns>{ "SEATCAP"      , &CapacityCsvColumns::seat_capacity }
            };
        }

        constexpr auto capacity_int_row_field_specs() {
            return std::array<csv_parse::Int64RowFieldSpec<CapacityCsvColumns, ParsedCapacityRow>, 2>{
                  csv_parse::Int64RowFieldSpec<CapacityCsvColumns, ParsedCapacityRow>{
                      "_VEHJOURNEYID", &CapacityCsvColumns::vehicle_journey_id, &ParsedCapacityRow::vehicle_journey_id
                  }
                , csv_parse::Int64RowFieldSpec<CapacityCsvColumns, ParsedCapacityRow>{
                      "INDEX", &CapacityCsvColumns::index, &ParsedCapacityRow::index
                  }
            };
        }

        constexpr auto capacity_double_row_field_specs() {
            return std::array<csv_parse::DoubleRowFieldSpec<CapacityCsvColumns, ParsedCapacityRow>, 2>{
                  csv_parse::DoubleRowFieldSpec<CapacityCsvColumns, ParsedCapacityRow>{
                      "TOTALCAP", &CapacityCsvColumns::total_capacity, &ParsedCapacityRow::total_capacity
                  }
                , csv_parse::DoubleRowFieldSpec<CapacityCsvColumns, ParsedCapacityRow>{
                      "SEATCAP", &CapacityCsvColumns::seat_capacity, &ParsedCapacityRow::seat_capacity
                  }
            };
        }

        mathfp::Expected<ParsedCapacityRow> parse_capacity_row(
              const ::csv::CSVRow&      row_data
            , const CapacityCsvColumns& columns
            , std::size_t               row
        ) {
            ParsedCapacityRow parsed_row{};
            MATHFP_TRY(csv_parse::decode_int64_row_fields(
                  parsed_row
                , row_data
                , columns
                , row
                , capacity_int_row_field_specs()
            ));
            MATHFP_TRY(csv_parse::decode_double_row_fields(
                  parsed_row
                , row_data
                , columns
                , row
                , capacity_double_row_field_specs()
            ));
            return parsed_row;
        }

        mathfp::Expected<VehicleJourneyItemCapacity> build_capacity(
              const ParsedCapacityRow& row
            , std::size_t              csv_row
        ) {
            auto capacity = timetable::domain::assignment::make_vehicle_journey_item_capacity(
                  VehicleJourneyItemKey{
                      .trip       = TripId{ row.vehicle_journey_id }
                    , .from_index = RoutePosition{ row.index }
                  }
                , row.total_capacity
                , row.seat_capacity
            );
            if (!capacity) {
                auto err = std::move(capacity.error());
                err.ctx("row", static_cast<std::int64_t>(csv_row));
                return mathfp::unexpected(std::move(err));
            }
            return *capacity;
        }

        mathfp::Expected<std::vector<VehicleJourneyItemCapacity>> parse_capacity_rows(
              ::csv::CSVReader&            reader
            , const CapacityCsvColumns&    columns
            , const std::filesystem::path& path
        ) {
            std::vector<VehicleJourneyItemCapacity> capacities;
            ::csv::CSVRow row_data;
            std::size_t row = 1;

            try {
                while (reader.read_row(row_data)) {
                    ++row;
                    MATHFP_TRY_LET(ParsedCapacityRow, parsed_row, parse_capacity_row(row_data, columns, row));
                    MATHFP_TRY_LET(VehicleJourneyItemCapacity, capacity, build_capacity(parsed_row, row));
                    capacities.push_back(std::move(capacity));
                }
            } catch (const std::exception& e) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed while reading vehicle journey item capacity csv rows")
                        .ctx("path"  , path.string())
                        .ctx("row"   , static_cast<std::int64_t>(row))
                        .ctx("reason", std::string(e.what()))
                );
            }

            return capacities;
        }

    }  // namespace

    mathfp::Expected<VehicleJourneyItemCapacitySet> parse_vehicle_journey_item_capacity_csv(
        const std::filesystem::path& path
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        status("parsing: opening vehicle journey item capacity csv");
        auto reader_result = csv_parse::open_csv(path, "veh_journey_item_cap.csv");
        if (!reader_result) {
            return mathfp::unexpected(std::move(reader_result.error()));
        }
        auto reader = std::move(*reader_result);

        status("parsing: reading vehicle journey item capacity csv header");
        MATHFP_TRY_LET(
              CapacityCsvColumns
            , columns
            , csv_parse::parse_csv_header<CapacityCsvColumns>(
                  reader
                , path
                , capacity_csv_column_defs()
                , "veh_journey_item_cap.csv"
            )
        );

        MATHFP_TRY_LET(
              std::vector<VehicleJourneyItemCapacity>
            , capacities
            , parse_capacity_rows(reader, columns, path)
        );
        MATHFP_TRY_LET(
              VehicleJourneyItemCapacitySet
            , capacity_set
            , timetable::domain::assignment::make_vehicle_journey_item_capacity_set(
                  std::move(capacities)
              )
        );

        log(
            fmt::format(
                  "parsing: vehicle journey item capacity csv parsed; items = {}"
                , capacity_set.items.size()
            )
            , LogLevel::Info
        );
        status("parsing: vehicle journey item capacity csv parsed");
        return capacity_set;
    }

}  // namespace timetable::infra::csv
