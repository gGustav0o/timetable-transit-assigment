#include "timetable/infra/txt_segments.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include <fmt/format.h>

#include "timetable/domain/preprocessing/segments_factory.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::txt {

	namespace {

		constexpr std::size_t kExpectedLines = 11;
		constexpr std::int64_t kMissingId = -1;
		constexpr std::size_t kStopReserveDiv = 2;
		constexpr std::size_t kLineReserveDiv = 4;
		constexpr std::size_t kExtraZoneReserveDiv = 16;
		constexpr std::size_t kReservePadding = 1;
		constexpr std::size_t kProgressStep = 100'000;

		std::string_view trim_cr(std::string_view line) {
			if (!line.empty() && line.back() == '\r') {
				line.remove_suffix(1);
			}
			return line;
		}

		mathfp::Unexpected parse_error(
			const char* message
			, std::string_view field
			, std::size_t line_no
			, std::size_t index
		) {
			return mathfp::unexpected(
				mathfp::invalid_arg(message)
				.ctx("field", std::string(field))
				.ctx("line", static_cast<std::int64_t>(line_no))
				.ctx("index", static_cast<std::int64_t>(index))
			);
		}

		mathfp::Expected<std::vector<std::int64_t>> parse_ints(
			std::string_view line
			, std::string_view field
			, std::size_t line_no
		) {
			std::vector<std::int64_t> out;
			line = trim_cr(line);

			const char* p = line.data();
			const char* end = p + line.size();
			std::size_t idx = 0;

			while (p < end) {
				while (p < end && std::isspace(static_cast<unsigned char>(*p)))
					++p;
				if (p >= end)
					break;
				errno = 0;
				char* next = nullptr;
				const auto v = std::strtoll(p, &next, 10);
				if (next == p) {
					return parse_error("failed to parse integer", field, line_no, idx);
				}
				if (errno == ERANGE) {
					return parse_error("integer out of range", field, line_no, idx);
				}
				out.push_back(static_cast<std::int64_t>(v));
				p = next;
				++idx;
			}
			return out;
		}

		mathfp::Expected<std::vector<double>> parse_doubles(
			std::string_view line
			, std::string_view field
			, std::size_t line_no
		) {
			std::vector<double> out;
			line = trim_cr(line);

			const char* p = line.data();
			const char* end = p + line.size();
			std::size_t idx = 0;

			while (p < end) {
				while (p < end && std::isspace(static_cast<unsigned char>(*p)))
					++p;
				if (p >= end)
					break;
				errno = 0;
				char* next = nullptr;
				const auto v = std::strtod(p, &next);
				if (next == p) {
					return parse_error("failed to parse floating point", field, line_no, idx);
				}
				if (errno == ERANGE) {
					return parse_error("floating point out of range", field, line_no, idx);
				}
				out.push_back(v);
				p = next;
				++idx;
			}
			return out;
		}

		mathfp::Expected<timetable::domain::WalkEndpoint> parse_endpoint(
			std::int64_t zone_id
			, std::int64_t stop_id
			, std::string_view field
			, std::size_t index
			, const std::unordered_set<std::int64_t>& zones
		) {
			using timetable::domain::StopId;
			using timetable::domain::ZoneId;

			const auto zone_missing = (zone_id == kMissingId);
			const auto stop_missing = (stop_id == kMissingId);

			if (zone_missing == stop_missing) {
				return mathfp::unexpected(
					mathfp::invalid_arg("exactly one of zone_id or stop_id must be set")
					.ctx("field", std::string(field))
					.ctx("index", static_cast<std::int64_t>(index))
				);
			}

			if (!zone_missing) {
				if (zone_id < 0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("zone_id must be non-negative")
						.ctx("field", std::string(field))
						.ctx("index", static_cast<std::int64_t>(index))
						.ctx("zone_id", zone_id)
					);
				}
				return timetable::domain::WalkEndpoint{ ZoneId{ zone_id } };
			}

			if (stop_id < 0) {
				return mathfp::unexpected(
					mathfp::invalid_arg("stop_id must be non-negative")
					.ctx("field", std::string(field))
					.ctx("index", static_cast<std::int64_t>(index))
					.ctx("stop_id", stop_id)
				);
			}

			return timetable::domain::WalkEndpoint{ StopId{ stop_id } };
		}

		bool same_endpoint(
			const timetable::domain::WalkEndpoint& a
			, const timetable::domain::WalkEndpoint& b
		) {
			if (a.index() != b.index())
				return false;
			if (std::holds_alternative<timetable::domain::StopId>(a))
				return std::get<timetable::domain::StopId>(a) == std::get<timetable::domain::StopId>(b);
			return std::get<timetable::domain::ZoneId>(a) == std::get<timetable::domain::ZoneId>(b);
		}

	}  // namespace

	mathfp::Expected<SegmentColumns> parse_segments_file(
		const std::filesystem::path& path
	) {
		using timetable::infra::LogLevel;
		using timetable::infra::progress::status;
		using timetable::infra::progress::log;

		status("parsing: opening txt input");
		log(
			"parsing: reading lines"
			, LogLevel::Info
		);

		std::ifstream input(path);
		if (!input.is_open()) {
			return mathfp::unexpected(
				mathfp::invalid_arg("failed to open input file")
				.ctx("path", path.string())
			);
		}

		std::vector<std::string> lines;
		lines.reserve(kExpectedLines);
		for (std::string line; std::getline(input, line); ) {
			if (line.empty())
				continue;
			lines.push_back(std::move(line));
		}

		if (lines.size() != kExpectedLines) {
			return mathfp::unexpected(
				mathfp::invalid_arg("unexpected number of lines in segment file")
				.ctx("lines", static_cast<std::int64_t>(lines.size()))
				.ctx("expected", static_cast<std::int64_t>(kExpectedLines))
			);
		}

		log(
			fmt::format("parsing: lines loaded = {}", lines.size())
			, LogLevel::Info
		);
		log("parsing: parsing columns", LogLevel::Info);
		SegmentColumns out;
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, from_zone_id
			, parse_ints(lines[0], "from_zone_id", 1)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, from_stop_id
			, parse_ints(lines[1], "from_stop_id", 2)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, to_zone_id
			, parse_ints(lines[2], "to_zone_id", 3)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, to_stop_id
			, parse_ints(lines[3], "to_stop_id", 4)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, profile_id
			, parse_ints(lines[4], "profile_id", 5)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, length_km
			, parse_doubles(lines[5], "length", 6)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, time_sec
			, parse_doubles(lines[6], "time", 7)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, dep_sec
			, parse_doubles(lines[7], "dep", 8)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, arr_sec
			, parse_doubles(lines[8], "arr", 9)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, fare
			, parse_doubles(lines[9], "fare", 10)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, zone_ids
			, parse_ints(lines[10], "zone_ids", 11)
		);

		out.from_zone_id = std::move(from_zone_id);
		out.from_stop_id = std::move(from_stop_id);
		out.to_zone_id = std::move(to_zone_id);
		out.to_stop_id = std::move(to_stop_id);
		out.profile_id = std::move(profile_id);
		out.length_km = std::move(length_km);
		out.time_sec = std::move(time_sec);
		out.dep_sec = std::move(dep_sec);
		out.arr_sec = std::move(arr_sec);
		out.fare = std::move(fare);
		out.zone_ids = std::move(zone_ids);

		log(
			fmt::format(
				"parsing: columns parsed; segments = {}  zones = {}"
				, out.from_zone_id.size()
				, out.zone_ids.size()
			),
			LogLevel::Info
		);
		status("parsing: columns parsed");

		return out;
	}

	mathfp::Expected<timetable::domain::AssignmentInput> build_assignment_input(
		SegmentColumns columns
		, const ParseParams& params
	) {
		using timetable::infra::LogLevel;
		using timetable::infra::progress::status;
		using timetable::infra::progress::log;

		status("parsing: validating columns");
		using timetable::domain::AssignmentInput;
		using timetable::domain::ConnectionSegment;
		using timetable::domain::ConnectionSegmentId;
		using timetable::domain::InputModel;
		using timetable::domain::Length;
		using timetable::domain::Line;
		using timetable::domain::LineId;
		using timetable::domain::RouteSegment;
		using timetable::domain::RouteSegmentId;
		using timetable::domain::Stop;
		using timetable::domain::StopId;
		using timetable::domain::Time;
		using timetable::domain::WalkLinkId;
		using timetable::domain::WalkPath;
		using timetable::domain::Zone;
		using timetable::domain::ZoneId;
		using timetable::domain::preprocessing::make_connection_segment;
		using timetable::domain::preprocessing::make_route_segment;

		const auto n = columns.from_zone_id.size();
		if (n == 0) {
			return mathfp::unexpected(
				mathfp::invalid_arg("segment file contains no segments")
			);
		}
		const auto require_same =
			[&](const std::vector<std::int64_t>& v, std::string_view name)
			-> mathfp::Expected<mathfp::Unit> {
			if (v.size() != n) {
				return mathfp::unexpected(
					mathfp::invalid_arg("segment column length mismatch")
					.ctx("column", std::string(name))
					.ctx("expected", static_cast<std::int64_t>(n))
					.ctx("actual", static_cast<std::int64_t>(v.size()))
				);
			}
			return mathfp::kUnit;
		};
		const auto require_same_d =
			[&](const std::vector<double>& v, std::string_view name)
			-> mathfp::Expected<mathfp::Unit> {
			if (v.size() != n) {
				return mathfp::unexpected(
					mathfp::invalid_arg("segment column length mismatch")
					.ctx("column", std::string(name))
					.ctx("expected", static_cast<std::int64_t>(n))
					.ctx("actual", static_cast<std::int64_t>(v.size()))
				);
			}
			return mathfp::kUnit;
		};

		if (auto r = require_same(columns.from_stop_id, "from_stop_id"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same(columns.to_zone_id, "to_zone_id"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same(columns.to_stop_id, "to_stop_id"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same(columns.profile_id, "profile_id"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same_d(columns.length_km, "length"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same_d(columns.time_sec, "time"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same_d(columns.dep_sec, "dep"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same_d(columns.arr_sec, "arr"); !r) return mathfp::unexpected(r.error());
		if (auto r = require_same_d(columns.fare, "fare"); !r) return mathfp::unexpected(r.error());

		log(
			fmt::format(
				"parsing: validated columns; segments = {}"
				, n
			),
			LogLevel::Info
		);
		log("parsing: building zones, stops, lines", LogLevel::Info);
		std::unordered_set<std::int64_t> zones_set;
		zones_set.reserve(columns.zone_ids.size());
		for (std::size_t i = 0; i < columns.zone_ids.size(); ++i) {
			const auto id = columns.zone_ids[i];
			if (id < 0) {
				return mathfp::unexpected(
					mathfp::invalid_arg("zone_ids must be non-negative")
					.ctx("zone_id", id)
					.ctx("index", static_cast<std::int64_t>(i))
				);
			}
			if (!zones_set.insert(id).second) {
				return mathfp::unexpected(
					mathfp::invalid_arg("duplicate zone_id")
					.ctx("zone_id", id)
					.ctx("index", static_cast<std::int64_t>(i))
				);
			}
		}

		std::unordered_set<std::int64_t> stop_ids;
		stop_ids.reserve(n / kStopReserveDiv + kReservePadding);
		std::unordered_set<std::int64_t> line_ids;
		line_ids.reserve(n / kLineReserveDiv + kReservePadding);
		std::unordered_set<std::int64_t> extra_zone_ids;
		extra_zone_ids.reserve(n / kExtraZoneReserveDiv + kReservePadding);

		std::vector<RouteSegment> route_segments;
		std::vector<ConnectionSegment> connection_segments;
		route_segments.reserve(n);
		connection_segments.reserve(n);

		std::int64_t next_walk_id = 0;

		log(
			fmt::format(
				"parsing: zone_ids = {}"
				, zones_set.size()
			),
			LogLevel::Info
		);
		log("parsing: building segments", LogLevel::Info);
		status("parsing: building segments (0%)");
		std::size_t walk_segments = 0;
		std::size_t line_segments = 0;
		std::size_t timed_segments = 0;
		std::size_t untimed_segments = 0;
		for (std::size_t i = 0; i < n; ++i) {
			if (i != 0 && (i % kProgressStep == 0)) {
				const auto pct = static_cast<int>(100.0 * static_cast<double>(i) / static_cast<double>(n));
				status(fmt::format("parsing: building segments ({}%)", pct));
				log(
					fmt::format(
						"parsing: progress i={}  walk={}  line={}  timed={}  untimed={}"
						, i
						, walk_segments
						, line_segments
						, timed_segments
						, untimed_segments
					),
					LogLevel::Debug
				);
			}
			const auto from_zone = columns.from_zone_id[i];
			const auto from_stop = columns.from_stop_id[i];
			const auto to_zone = columns.to_zone_id[i];
			const auto to_stop = columns.to_stop_id[i];
			const auto profile = columns.profile_id[i];

			MATHFP_TRY_LET(
				timetable::domain::WalkEndpoint
				, from_endpoint
				, parse_endpoint(from_zone, from_stop, "from", i, zones_set)
			);
			MATHFP_TRY_LET(
				timetable::domain::WalkEndpoint
				, to_endpoint
				, parse_endpoint(to_zone, to_stop, "to", i, zones_set)
			);

			if (same_endpoint(from_endpoint, to_endpoint)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("segment endpoints must be distinct")
					.ctx("index", static_cast<std::int64_t>(i))
					.ctx("from_zone_id", from_zone)
					.ctx("from_stop_id", from_stop)
					.ctx("to_zone_id", to_zone)
					.ctx("to_stop_id", to_stop)
					.ctx("profile_id", profile)
					.ctx("length", columns.length_km[i])
					.ctx("time", columns.time_sec[i])
					.ctx("dep", columns.dep_sec[i])
					.ctx("arr", columns.arr_sec[i])
					.ctx("fare", columns.fare[i])
				);
			}

			if (std::holds_alternative<ZoneId>(from_endpoint)) {
				const auto zid = std::get<ZoneId>(from_endpoint).get();
				if (!zones_set.contains(zid)) {
					extra_zone_ids.insert(zid);
				}
			}
			if (std::holds_alternative<ZoneId>(to_endpoint)) {
				const auto zid = std::get<ZoneId>(to_endpoint).get();
				if (!zones_set.contains(zid)) {
					extra_zone_ids.insert(zid);
				}
			}

			if (std::holds_alternative<StopId>(from_endpoint))
				stop_ids.insert(std::get<StopId>(from_endpoint).get());
			if (std::holds_alternative<StopId>(to_endpoint))
				stop_ids.insert(std::get<StopId>(to_endpoint).get());

			const auto is_walk_segment = (profile == kMissingId);
			if (!is_walk_segment) {
				if (profile < 0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("profile_id must be -1 or non-negative")
						.ctx("index", static_cast<std::int64_t>(i))
						.ctx("profile_id", profile)
					);
				}
				line_ids.insert(profile);
			}

			timetable::domain::SegmentCarrier carrier;
			if (is_walk_segment) {
				++walk_segments;
				carrier = WalkPath{ WalkLinkId{ next_walk_id++ } };
			} else {
				++line_segments;
				if (!std::holds_alternative<StopId>(from_endpoint)
					|| !std::holds_alternative<StopId>(to_endpoint)) {
					return mathfp::unexpected(
						mathfp::invalid_arg("line segment endpoints must be stops")
						.ctx("index", static_cast<std::int64_t>(i))
						.ctx("profile_id", profile)
					);
				}
				carrier = LineId{ profile };
			}

			MATHFP_TRY_LET(
				RouteSegment
				, route
				, make_route_segment(
					RouteSegmentId{ static_cast<std::int64_t>(i) }
					, from_endpoint
					, to_endpoint
					, Length{ columns.length_km[i] }
					, Time{ columns.time_sec[i] }
					, std::move(carrier)
				)
			);
			route_segments.push_back(std::move(route));

			std::optional<Time> dep{};
			std::optional<Time> arr{};
			const auto dep_val = columns.dep_sec[i];
			const auto arr_val = columns.arr_sec[i];
			if (dep_val >= 0.0 || arr_val >= 0.0) {
				if (!(dep_val >= 0.0 && arr_val >= 0.0)) {
					return mathfp::unexpected(
						mathfp::invalid_arg("departure and arrival must both be set or both be -1")
						.ctx("index", static_cast<std::int64_t>(i))
						.ctx("dep", dep_val)
						.ctx("arr", arr_val)
					);
				}
				dep = Time{ dep_val };
				arr = Time{ arr_val };
				++timed_segments;
			} else {
				++untimed_segments;
			}

			std::optional<double> fare{};
			const auto fare_val = columns.fare[i];
			if (fare_val >= 0.0) {
				fare = fare_val;
			}

			MATHFP_TRY_LET(
				ConnectionSegment
				, conn
				, make_connection_segment(
					ConnectionSegmentId{ static_cast<std::int64_t>(i) }
					, route_segments.back()
					, std::move(dep)
					, std::move(arr)
					, std::move(fare)
				)
			);
			connection_segments.push_back(std::move(conn));
		}

		status("parsing: building segments (100%)");
		log(
			fmt::format(
				"parsing: segments built; route_segments = {}  connection_segments = {}"
				"  walk = {}  line = {}  timed = {}  untimed = {}"
				, route_segments.size()
				, connection_segments.size()
				, walk_segments
				, line_segments
				, timed_segments
				, untimed_segments
			),
			LogLevel::Info
		);
		if (!extra_zone_ids.empty()) {
			if (!params.allow_unknown_zones) {
				std::vector<std::int64_t> extra_list(extra_zone_ids.begin(), extra_zone_ids.end());
				std::sort(extra_list.begin(), extra_list.end());
				const auto sample = std::min<std::size_t>(extra_list.size(), 10);
				std::string sample_str;
				for (std::size_t i = 0; i < sample; ++i) {
					if (i > 0) sample_str += ", ";
					sample_str += std::to_string(extra_list[i]);
				}
				return mathfp::unexpected(
					mathfp::invalid_arg("segment zones not listed in zone_ids")
					.ctx("count", static_cast<std::int64_t>(extra_list.size()))
					.ctx("sample", sample_str)
				);
			}
			log(
				fmt::format(
					"parsing: zone_ids extended by {} extra zones from segments"
					, extra_zone_ids.size()
				),
				LogLevel::Warning
			);
		}

		InputModel input;
		input.zones.reserve(columns.zone_ids.size() + extra_zone_ids.size());
		for (const auto id : columns.zone_ids) {
			input.zones.push_back(Zone{ ZoneId{ id } });
		}
		if (!extra_zone_ids.empty()) {
			std::vector<std::int64_t> extra_list(extra_zone_ids.begin(), extra_zone_ids.end());
			std::sort(extra_list.begin(), extra_list.end());
			for (const auto id : extra_list) {
				input.zones.push_back(Zone{ ZoneId{ id } });
			}
		}

		input.stops.reserve(stop_ids.size());
		std::vector<std::int64_t> stop_list(stop_ids.begin(), stop_ids.end());
		std::sort(stop_list.begin(), stop_list.end());
		for (const auto id : stop_list) {
			input.stops.push_back(Stop{ StopId{ id }, std::nullopt });
		}

		input.lines.reserve(line_ids.size());
		std::vector<std::int64_t> line_list(line_ids.begin(), line_ids.end());
		std::sort(line_list.begin(), line_list.end());
		for (const auto id : line_list) {
			input.lines.push_back(Line{ LineId{ id } });
		}

		log(
			fmt::format(
				"parsing: model entities; zones = {}  stops = {}  lines = {}"
				, input.zones.size()
				, input.stops.size()
				, input.lines.size()
			),
			LogLevel::Info
		);

		status("parsing: building assignment input");
		AssignmentInput out;
		out.input = std::move(input);
		out.params = timetable::domain::SearchParams{};
		out.presegmented = AssignmentInput::PresegmentedInput{
			.route_segments = std::move(route_segments),
			.connection_segments = std::move(connection_segments)
		};
		log("parsing: assignment input ready", LogLevel::Info);
		status("parsing: done");
		return out;
	}

}  // namespace timetable::infra::txt
