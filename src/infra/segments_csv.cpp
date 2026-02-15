#include "timetable/infra/segments_csv.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <mathfp/core/error.hpp>

namespace timetable::infra::csv {

	namespace {

		std::string trim(std::string_view in) {
			std::size_t b = 0;
			std::size_t e = in.size();
			while (b < e && std::isspace(static_cast<unsigned char>(in[b]))) ++b;
			while (e > b && std::isspace(static_cast<unsigned char>(in[e - 1]))) --e;
			return std::string(in.substr(b, e - b));
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

		std::vector<std::string> parse_csv_row(
			const std::string& line
		) {
			std::vector<std::string> out;
			std::string field;
			bool in_quotes = false;

			for (std::size_t i = 0; i < line.size(); ++i) {
				const char c = line[i];
				if (in_quotes) {
					if (c == '"') {
						if (i + 1 < line.size() && line[i + 1] == '"') {
							field.push_back('"');
							++i;
						} else {
							in_quotes = false;
						}
					} else {
						field.push_back(c);
					}
					continue;
				}

				if (c == '"') {
					in_quotes = true;
					continue;
				}
				if (c == ',') {
					out.push_back(trim(field));
					field.clear();
					continue;
				}
				field.push_back(c);
			}

			out.push_back(trim(field));
			return out;
		}

		mathfp::Expected<std::int64_t> parse_int64_cell(
			std::string_view text
			, std::size_t row
			, std::string_view column
		) {
			const auto t = trim(text);
			if (t.empty()) {
				return parse_error("empty integer field", row, column);
			}

			errno = 0;
			char* end = nullptr;
			const auto value = std::strtoll(t.c_str(), &end, 10);
			if (end == t.c_str() || *end != '\0') {
				return parse_error("failed to parse integer", row, column);
			}
			if (errno == ERANGE) {
				return parse_error("integer out of range", row, column);
			}

			return static_cast<std::int64_t>(value);
		}

		mathfp::Expected<double> parse_double_cell(
			std::string_view text
			, std::size_t row
			, std::string_view column
		) {
			const auto t = trim(text);
			if (t.empty()) {
				return parse_error("empty floating point field", row, column);
			}

			errno = 0;
			char* end = nullptr;
			const auto value = std::strtod(t.c_str(), &end);
			if (end == t.c_str() || *end != '\0') {
				return parse_error("failed to parse floating point", row, column);
			}
			if (errno == ERANGE) {
				return parse_error("floating point out of range", row, column);
			}

			return value;
		}

		mathfp::Expected<std::size_t> find_col(
			const std::unordered_map<std::string, std::size_t>& cols
			, std::string_view name
		) {
			const auto it = cols.find(std::string(name));
			if (it == cols.end()) {
				return mathfp::unexpected(
					mathfp::invalid_arg("missing required csv column")
					.ctx("column", std::string(name))
				);
			}
			return it->second;
		}

	}  // namespace

	mathfp::Expected<txt::SegmentColumns> parse_connection_segments_csv(
		const std::filesystem::path& path
	) {
		std::ifstream input(path);
		if (!input.is_open()) {
			return mathfp::unexpected(
				mathfp::invalid_arg("failed to open connection segments csv")
				.ctx("path", path.string())
			);
		}

		std::string header_line;
		if (!std::getline(input, header_line)) {
			return mathfp::unexpected(
				mathfp::invalid_arg("connection segments csv is empty")
				.ctx("path", path.string())
			);
		}

		auto header = parse_csv_row(header_line);
		for (auto& cell : header) {
			cell = trim(cell);
		}

		std::unordered_map<std::string, std::size_t> cols;
		cols.reserve(header.size());
		for (std::size_t i = 0; i < header.size(); ++i) {
			if (!header[i].empty()) {
				cols.emplace(header[i], i);
			}
		}

		const auto from_stop_col = find_col(cols, "FROM_STOP_ID");
		if (!from_stop_col) return mathfp::unexpected(from_stop_col.error());
		const auto to_stop_col = find_col(cols, "TO_STOP_ID");
		if (!to_stop_col) return mathfp::unexpected(to_stop_col.error());
		const auto time_col = find_col(cols, "TIME");
		if (!time_col) return mathfp::unexpected(time_col.error());
		const auto length_col = find_col(cols, "LENGTH");
		if (!length_col) return mathfp::unexpected(length_col.error());
		const auto from_zone_col = find_col(cols, "FROM_ZONE_ID");
		if (!from_zone_col) return mathfp::unexpected(from_zone_col.error());
		const auto to_zone_col = find_col(cols, "TO_ZONE_ID");
		if (!to_zone_col) return mathfp::unexpected(to_zone_col.error());
		const auto fare_col = find_col(cols, "FARE");
		if (!fare_col) return mathfp::unexpected(fare_col.error());
		const auto line_col = find_col(cols, "LINE_ID");
		if (!line_col) return mathfp::unexpected(line_col.error());
		const auto dep_col = find_col(cols, "DEP");
		if (!dep_col) return mathfp::unexpected(dep_col.error());
		const auto arr_col = find_col(cols, "ARR");
		if (!arr_col) return mathfp::unexpected(arr_col.error());

		txt::SegmentColumns out;
		std::unordered_set<std::int64_t> zone_set;
		zone_set.reserve(256);

		std::string line;
		std::size_t row = 1;
		while (std::getline(input, line)) {
			++row;
			if (line.empty()) {
				continue;
			}

			const auto cells = parse_csv_row(line);
			const auto max_col = std::max({
				*from_stop_col, *to_stop_col, *time_col, *length_col, *from_zone_col,
				*to_zone_col, *fare_col, *line_col, *dep_col, *arr_col
			});
			if (cells.size() <= max_col) {
				return mathfp::unexpected(
					mathfp::invalid_arg("csv row has fewer columns than required")
					.ctx("row", static_cast<std::int64_t>(row))
					.ctx("columns", static_cast<std::int64_t>(cells.size()))
					.ctx("required_min", static_cast<std::int64_t>(max_col + 1))
				);
			}

			const auto from_zone = parse_int64_cell(cells[*from_zone_col], row, "FROM_ZONE_ID");
			if (!from_zone) return mathfp::unexpected(from_zone.error());
			const auto from_stop = parse_int64_cell(cells[*from_stop_col], row, "FROM_STOP_ID");
			if (!from_stop) return mathfp::unexpected(from_stop.error());
			const auto to_zone = parse_int64_cell(cells[*to_zone_col], row, "TO_ZONE_ID");
			if (!to_zone) return mathfp::unexpected(to_zone.error());
			const auto to_stop = parse_int64_cell(cells[*to_stop_col], row, "TO_STOP_ID");
			if (!to_stop) return mathfp::unexpected(to_stop.error());
			const auto profile = parse_int64_cell(cells[*line_col], row, "LINE_ID");
			if (!profile) return mathfp::unexpected(profile.error());
			const auto length = parse_double_cell(cells[*length_col], row, "LENGTH");
			if (!length) return mathfp::unexpected(length.error());
			const auto time = parse_double_cell(cells[*time_col], row, "TIME");
			if (!time) return mathfp::unexpected(time.error());
			const auto dep = parse_double_cell(cells[*dep_col], row, "DEP");
			if (!dep) return mathfp::unexpected(dep.error());
			const auto arr = parse_double_cell(cells[*arr_col], row, "ARR");
			if (!arr) return mathfp::unexpected(arr.error());
			const auto fare = parse_double_cell(cells[*fare_col], row, "FARE");
			if (!fare) return mathfp::unexpected(fare.error());

			out.from_zone_id.push_back(*from_zone);
			out.from_stop_id.push_back(*from_stop);
			out.to_zone_id.push_back(*to_zone);
			out.to_stop_id.push_back(*to_stop);
			out.profile_id.push_back(*profile);
			out.length_km.push_back(*length);
			out.time_sec.push_back(*time);
			out.dep_sec.push_back(*dep);
			out.arr_sec.push_back(*arr);
			out.fare.push_back(*fare);

			if (*from_zone >= 0) zone_set.insert(*from_zone);
			if (*to_zone >= 0) zone_set.insert(*to_zone);
		}

		out.zone_ids.assign(zone_set.begin(), zone_set.end());
		std::sort(out.zone_ids.begin(), out.zone_ids.end());

		if (out.from_zone_id.empty()) {
			return mathfp::unexpected(
				mathfp::invalid_arg("connection segments csv contains no data rows")
				.ctx("path", path.string())
			);
		}

		return out;
	}

}  // namespace timetable::infra::csv
