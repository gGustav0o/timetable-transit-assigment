#include "timetable/infra/segments_csv.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <csv.hpp>
#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include <fmt/format.h>

#include "timetable/infra/progress_bus.hpp"
#include "timetable/infra/text_parse.hpp"

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
			std::int64_t line_id{};
			std::int64_t from_index{};
			std::int64_t to_index{};
			double length{};
			double time{};
			double dep{};
			double arr{};
			double fare{};
		};

		struct ParsedCsvHeader final {
			SegmentCsvColumns columns{};
			std::size_t header_column_count{};
		};

		struct ParsedCsvData final {
			txt::SegmentColumns columns{};
			std::unordered_set<std::int64_t> zone_set{};
		};

		using SegmentCsvColumnDef = std::pair<std::string_view, std::size_t SegmentCsvColumns::*>;

		constexpr auto segment_csv_column_defs() {
			return std::array<SegmentCsvColumnDef, 13>{
				SegmentCsvColumnDef{ "FROM_STOP_ID", &SegmentCsvColumns::from_stop },
				SegmentCsvColumnDef{ "TO_STOP_ID", &SegmentCsvColumns::to_stop },
				SegmentCsvColumnDef{ "TIME", &SegmentCsvColumns::time },
				SegmentCsvColumnDef{ "LENGTH", &SegmentCsvColumns::length },
				SegmentCsvColumnDef{ "FROM_ZONE_ID", &SegmentCsvColumns::from_zone },
				SegmentCsvColumnDef{ "TO_ZONE_ID", &SegmentCsvColumns::to_zone },
				SegmentCsvColumnDef{ "FARE", &SegmentCsvColumns::fare },
				SegmentCsvColumnDef{ "TRIP_ID", &SegmentCsvColumns::trip },
				SegmentCsvColumnDef{ "LINE_ID", &SegmentCsvColumns::line },
				SegmentCsvColumnDef{ "FROM_INDEX", &SegmentCsvColumns::from_index },
				SegmentCsvColumnDef{ "DEP", &SegmentCsvColumns::dep },
				SegmentCsvColumnDef{ "TO_INDEX", &SegmentCsvColumns::to_index },
				SegmentCsvColumnDef{ "ARR", &SegmentCsvColumns::arr }
			};
		}

		template <typename Columns, typename Visitor>
		void for_each_segment_csv_column(
			Columns& cols
			, Visitor&& visit
		) {
			for (const auto& [name, member] : segment_csv_column_defs()) {
				visit(name, cols.*member);
			}
		}

		template <typename Visitor>
		mathfp::Expected<mathfp::Unit> try_for_each_segment_csv_column(
			SegmentCsvColumns& cols
			, Visitor&& visit
		) {
			for (const auto& [name, member] : segment_csv_column_defs()) {
				MATHFP_TRY(visit(name, cols.*member));
			}
			return mathfp::ok();
		}

		mathfp::Unexpected parse_error(
			const char* message
			, std::size_t row
			, std::string_view column
		) {
			return mathfp::unexpected(
				mathfp::invalid_arg(message)
				.ctx("row", static_cast<std::int64_t>(row))
				.ctx("column", std::string(column))
			);
		}

		template <typename T, typename ParseFn>
		mathfp::Expected<T> parse_numeric_cell(
			std::string_view text
			, std::size_t row
			, std::string_view column
			, const char* empty_message
			, const char* invalid_message
			, const char* range_message
			, ParseFn&& parse
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

		mathfp::Expected<std::int64_t> parse_int64_cell(
			std::string_view text
			, std::size_t row
			, std::string_view column
		) {
			return parse_numeric_cell<std::int64_t>(
				text,
				row,
				column,
				"empty integer field",
				"failed to parse integer",
				"integer out of range",
				[](const char* begin, char** end) {
					return std::strtoll(begin, end, 10);
				}
			);
		}

		mathfp::Expected<double> parse_double_cell(
			std::string_view text
			, std::size_t row
			, std::string_view column
		) {
			return parse_numeric_cell<double>(
				text,
				row,
				column,
				"empty floating point field",
				"failed to parse floating point",
				"floating point out of range",
				[](const char* begin, char** end) {
					return std::strtod(begin, end);
				}
			);
		}

		mathfp::Expected<::csv::CSVReader> open_connection_segments_csv(
			const std::filesystem::path& path
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
					mathfp::invalid_arg("failed to open or initialize connection segments csv")
						.ctx("path", path.string())
						.ctx("reason", std::string(e.what()))
				);
			}
		}

		mathfp::Expected<SegmentCsvColumns> resolve_segment_csv_columns(
			const ::csv::CSVReader& reader
		) {
			SegmentCsvColumns resolved;
			MATHFP_TRY(try_for_each_segment_csv_column(
				resolved,
				[&reader](std::string_view name, std::size_t& column)
					-> mathfp::Expected<mathfp::Unit> {
					const auto resolved_column = reader.index_of(std::string(name));
					if (resolved_column == ::csv::CSV_NOT_FOUND) {
						return mathfp::unexpected(
							mathfp::invalid_arg("missing required csv column")
								.ctx("column", std::string(name))
						);
					}
					column = static_cast<std::size_t>(resolved_column);
					return mathfp::ok();
				}
			));
			return resolved;
		}

		mathfp::Expected<ParsedCsvHeader> parse_csv_header(
			const ::csv::CSVReader& reader
			, const std::filesystem::path& path
		) {
			const auto header = reader.get_col_names();
			if (header.empty()) {
				return mathfp::unexpected(
					mathfp::invalid_arg("connection segments csv is empty or has no header")
						.ctx("path", path.string())
				);
			}

			MATHFP_TRY_LET(SegmentCsvColumns, columns, resolve_segment_csv_columns(reader));

			return ParsedCsvHeader{
				.columns = columns,
				.header_column_count = header.size()
			};
		}

		std::string_view field_text(const ::csv::CSVField& field) {
			const auto sv = field.get_sv();
			return std::string_view(sv.data(), sv.size());
		}

		mathfp::Expected<ParsedSegmentRow> parse_segment_row(
			const ::csv::CSVRow& row_data
			, const SegmentCsvColumns& cols
			, std::size_t row
		) {
			MATHFP_TRY_LET(std::int64_t, from_zone, parse_int64_cell(field_text(row_data[cols.from_zone]), row, "FROM_ZONE_ID"));
			MATHFP_TRY_LET(std::int64_t, from_stop, parse_int64_cell(field_text(row_data[cols.from_stop]), row, "FROM_STOP_ID"));
			MATHFP_TRY_LET(std::int64_t, to_zone, parse_int64_cell(field_text(row_data[cols.to_zone]), row, "TO_ZONE_ID"));
			MATHFP_TRY_LET(std::int64_t, to_stop, parse_int64_cell(field_text(row_data[cols.to_stop]), row, "TO_STOP_ID"));
			MATHFP_TRY_LET(std::int64_t, trip_id, parse_int64_cell(field_text(row_data[cols.trip]), row, "TRIP_ID"));
			MATHFP_TRY_LET(std::int64_t, line_id, parse_int64_cell(field_text(row_data[cols.line]), row, "LINE_ID"));
			MATHFP_TRY_LET(std::int64_t, from_index, parse_int64_cell(field_text(row_data[cols.from_index]), row, "FROM_INDEX"));
			MATHFP_TRY_LET(std::int64_t, to_index, parse_int64_cell(field_text(row_data[cols.to_index]), row, "TO_INDEX"));
			MATHFP_TRY_LET(double, length, parse_double_cell(field_text(row_data[cols.length]), row, "LENGTH"));
			MATHFP_TRY_LET(double, time, parse_double_cell(field_text(row_data[cols.time]), row, "TIME"));
			MATHFP_TRY_LET(double, dep, parse_double_cell(field_text(row_data[cols.dep]), row, "DEP"));
			MATHFP_TRY_LET(double, arr, parse_double_cell(field_text(row_data[cols.arr]), row, "ARR"));
			MATHFP_TRY_LET(double, fare, parse_double_cell(field_text(row_data[cols.fare]), row, "FARE"));

			return ParsedSegmentRow{
				.from_zone = from_zone,
				.from_stop = from_stop,
				.to_zone = to_zone,
				.to_stop = to_stop,
				.trip_id = trip_id,
				.line_id = line_id,
				.from_index = from_index,
				.to_index = to_index,
				.length = length,
				.time = time,
				.dep = dep,
				.arr = arr,
				.fare = fare
			};
		}

		void append_segment_row(
			txt::SegmentColumns& out
			, const ParsedSegmentRow& row
		) {
			out.from_zone_id.push_back(row.from_zone);
			out.from_stop_id.push_back(row.from_stop);
			out.to_zone_id.push_back(row.to_zone);
			out.to_stop_id.push_back(row.to_stop);
			out.profile_id.push_back(row.line_id);
			out.trip_id.push_back(row.trip_id);
			out.from_index.push_back(row.from_index);
			out.to_index.push_back(row.to_index);
			out.length_km.push_back(row.length);
			out.time_sec.push_back(row.time);
			out.dep_sec.push_back(row.dep);
			out.arr_sec.push_back(row.arr);
			out.fare.push_back(row.fare);
		}

		void collect_row_zones(
			std::unordered_set<std::int64_t>& zone_set
			, const ParsedSegmentRow& row
		) {
			if (row.from_zone >= 0) zone_set.insert(row.from_zone);
			if (row.to_zone >= 0) zone_set.insert(row.to_zone);
		}

		void finalize_zone_ids(
			txt::SegmentColumns& out
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
			::csv::CSVReader& reader
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
					append_segment_row(data.columns, parsed_row);
					collect_row_zones(data.zone_set, parsed_row);
				}
			} catch (const std::exception& e) {
				return mathfp::unexpected(
					mathfp::invalid_arg("failed while reading connection segments csv rows")
						.ctx("row", static_cast<std::int64_t>(row))
						.ctx("reason", std::string(e.what()))
				);
			}

			return data;
		}

		mathfp::Expected<txt::SegmentColumns> finalize_parsed_csv_data(
			ParsedCsvData data
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

	mathfp::Expected<txt::SegmentColumns> parse_connection_segments_csv(
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
		MATHFP_TRY_LET(txt::SegmentColumns, out, finalize_parsed_csv_data(std::move(parsed_data), path));
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
