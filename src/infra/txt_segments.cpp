#include "timetable/infra/txt_segments.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <boost/container_hash/hash.hpp>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include <fmt/format.h>

#include "timetable/domain/preprocessing/segments_factory.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::txt {

	namespace {

		constexpr std::size_t kExpectedLines = 11;
		constexpr std::int64_t kMissingId = -1;
		constexpr double kTimeConsistencyEps = 1e-9;
		constexpr std::size_t kStopReserveDiv = 2;
		constexpr std::size_t kLineReserveDiv = 4;
		constexpr std::size_t kExtraZoneReserveDiv = 16;
		constexpr std::size_t kReservePadding = 1;
		constexpr std::size_t kProgressStep = 100'000;
		constexpr std::string_view kCtxActual = "actual";
		constexpr std::string_view kCtxArr = "arr";
		constexpr std::string_view kCtxColumn = "column";
		constexpr std::string_view kCtxCount = "count";
		constexpr std::string_view kCtxDep = "dep";
		constexpr std::string_view kCtxExpected = "expected";
		constexpr std::string_view kCtxField = "field";
		constexpr std::string_view kCtxFromIndex = "from_index";
		constexpr std::string_view kCtxFromStopId = "from_stop_id";
		constexpr std::string_view kCtxFromZoneId = "from_zone_id";
		constexpr std::string_view kCtxIndex = "index";
		constexpr std::string_view kCtxLength = "length";
		constexpr std::string_view kCtxLineId = "line_id";
		constexpr std::string_view kCtxPath = "path";
		constexpr std::string_view kCtxProfileId = "profile_id";
		constexpr std::string_view kCtxSample = "sample";
		constexpr std::string_view kCtxStopId = "stop_id";
		constexpr std::string_view kCtxTime = "time";
		constexpr std::string_view kCtxToIndex = "to_index";
		constexpr std::string_view kCtxToStopId = "to_stop_id";
		constexpr std::string_view kCtxToZoneId = "to_zone_id";
		constexpr std::string_view kCtxTripId = "trip_id";
		constexpr std::string_view kCtxZoneId = "zone_id";

		constexpr std::string_view kFieldArr = "arr";
		constexpr std::string_view kFieldDep = "dep";
		constexpr std::string_view kFieldFare = "fare";
		constexpr std::string_view kFieldFrom = "from";
		constexpr std::string_view kFieldFromStopId = "from_stop_id";
		constexpr std::string_view kFieldFromZoneId = "from_zone_id";
		constexpr std::string_view kFieldLength = "length";
		constexpr std::string_view kFieldProfileId = "profile_id";
		constexpr std::string_view kFieldTime = "time";
		constexpr std::string_view kFieldTo = "to";
		constexpr std::string_view kFieldToStopId = "to_stop_id";
		constexpr std::string_view kFieldToZoneId = "to_zone_id";
		constexpr std::string_view kFieldTripId = "trip_id";
		constexpr std::string_view kFieldZoneIds = "zone_ids";
		constexpr std::string_view kFieldFromIndex = "from_index";
		constexpr std::string_view kFieldToIndex = "to_index";

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
				.ctx(std::string(kCtxField), std::string(field))
				.ctx("line", static_cast<std::int64_t>(line_no))
				.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(index))
			);
		}

		template <typename T, typename ParseFn>
		mathfp::Expected<std::vector<T>> parse_numeric_sequence(
			std::string_view line
			, std::string_view field
			, std::size_t line_no
			, const char* invalid_message
			, const char* range_message
			, ParseFn&& parse
		) {
			std::vector<T> out;
			line = trim_cr(line);

			const char* p = line.data();
			const char* end = p + line.size();
			std::size_t idx = 0;

			while (p < end) {
				while (p < end && std::isspace(static_cast<unsigned char>(*p))) {
					++p;
				}
				if (p >= end) {
					break;
				}

				errno = 0;
				char* next = nullptr;
				const auto value = parse(p, &next);
				if (next == p) {
					return parse_error(invalid_message, field, line_no, idx);
				}
				if (errno == ERANGE) {
					return parse_error(range_message, field, line_no, idx);
				}

				out.push_back(static_cast<T>(value));
				p = next;
				++idx;
			}

			return out;
		}

		mathfp::Expected<std::vector<std::int64_t>> parse_ints(
			std::string_view line
			, std::string_view field
			, std::size_t line_no
		) {
			return parse_numeric_sequence<std::int64_t>(
				line,
				field,
				line_no,
				"failed to parse integer",
				"integer out of range",
				[](const char* begin, char** end) {
					return std::strtoll(begin, end, 10);
				}
			);
		}

		mathfp::Expected<std::vector<double>> parse_doubles(
			std::string_view line
			, std::string_view field
			, std::size_t line_no
		) {
			return parse_numeric_sequence<double>(
				line,
				field,
				line_no,
				"failed to parse floating point",
				"floating point out of range",
				[](const char* begin, char** end) {
					return std::strtod(begin, end);
				}
			);
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
					.ctx(std::string(kCtxField), std::string(field))
					.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(index))
				);
			}

			if (!zone_missing) {
				if (zone_id < 0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("zone_id must be non-negative")
						.ctx(std::string(kCtxField), std::string(field))
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(index))
						.ctx(std::string(kCtxZoneId), zone_id)
					);
				}
				return timetable::domain::WalkEndpoint{ ZoneId{ zone_id } };
			}

			if (stop_id < 0) {
				return mathfp::unexpected(
					mathfp::invalid_arg("stop_id must be non-negative")
					.ctx(std::string(kCtxField), std::string(field))
					.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(index))
					.ctx(std::string(kCtxStopId), stop_id)
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

		mathfp::Expected<mathfp::Unit> ensure_time_matches_departure_arrival(
			double time_sec
			, double dep_sec
			, double arr_sec
			, std::size_t row_index
		) {
			const auto scheduled_time = arr_sec - dep_sec;
			if (std::abs(time_sec - scheduled_time) > kTimeConsistencyEps) {
				return mathfp::unexpected(
					mathfp::invalid_arg("TIME must equal ARR - DEP for timed segment")
					.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row_index))
					.ctx(std::string(kCtxTime), time_sec)
					.ctx(std::string(kCtxDep), dep_sec)
					.ctx(std::string(kCtxArr), arr_sec)
					.ctx("arr_minus_dep", scheduled_time)
				);
			}
			return mathfp::kUnit;
		}

		struct LineRouteKey final {
			timetable::domain::EndpointKey from{};
			timetable::domain::EndpointKey to{};
			std::int64_t line_id{};

			auto operator<=>(const LineRouteKey&) const = default;
		};

		struct LineRouteKeyHash final {
			std::size_t operator()(const LineRouteKey& key) const noexcept {
				std::size_t seed = 0;
				boost::hash_combine(seed, key.from);
				boost::hash_combine(seed, key.to);
				boost::hash_combine(seed, key.line_id);
				return seed;
			}
		};

		struct LineRouteMetrics final {
			double length_km{};
			double time_sec{};
		};

		struct LineRouteEntry final {
			timetable::domain::RouteSegmentId id{};
			LineRouteMetrics metrics{};
		};

		struct BuildStats final {
			std::size_t walk_segments{};
			std::size_t line_segments{};
			std::size_t timed_segments{};
			std::size_t untimed_segments{};
			std::size_t dropped_overnight{};
		};

		struct BuildState final {
			std::unordered_set<std::int64_t> zones_set{};
			std::unordered_set<std::int64_t> stop_ids{};
			std::unordered_set<std::int64_t> line_ids{};
			std::unordered_set<std::int64_t> extra_zone_ids{};
			std::unordered_map<LineRouteKey, LineRouteEntry, LineRouteKeyHash> line_routes{};
			std::vector<timetable::domain::RouteSegment> route_segments{};
			std::vector<timetable::domain::ConnectionSegment> connection_segments{};
			std::int64_t next_walk_id{};
			std::int64_t next_route_segment_id{};
			std::int64_t next_connection_segment_id{};
			BuildStats stats{};
		};

		struct SegmentRowView final {
			std::size_t index{};
			std::int64_t from_zone{};
			std::int64_t from_stop{};
			std::int64_t to_zone{};
			std::int64_t to_stop{};
			std::int64_t profile{};
			std::int64_t trip_id{};
			std::int64_t from_index{};
			std::int64_t to_index{};
			double length_km{};
			double time_sec{};
			double dep_sec{};
			double arr_sec{};
			double fare{};
		};

		struct SegmentSemantics final {
			timetable::domain::WalkEndpoint from_endpoint{};
			timetable::domain::WalkEndpoint to_endpoint{};
			bool is_walk_segment{};
			std::optional<timetable::domain::Time> dep{};
			std::optional<timetable::domain::Time> arr{};
			std::optional<double> fare{};
		};

		bool line_route_metrics_equal(
			const LineRouteMetrics& lhs
			, const LineRouteMetrics& rhs
		) {
			return mathfp::almost_equal(lhs.length_km, rhs.length_km)
				&& mathfp::almost_equal(lhs.time_sec, rhs.time_sec);
		}

		mathfp::Expected<mathfp::Unit> validate_int_column_length(
			const std::vector<std::int64_t>& values
			, std::string_view name
			, std::size_t expected
		) {
			if (values.size() != expected) {
				return mathfp::unexpected(
					mathfp::invalid_arg("segment column length mismatch")
					.ctx(std::string(kCtxColumn), std::string(name))
					.ctx(std::string(kCtxExpected), static_cast<std::int64_t>(expected))
					.ctx(std::string(kCtxActual), static_cast<std::int64_t>(values.size()))
				);
			}
			return mathfp::kUnit;
		}

		mathfp::Expected<mathfp::Unit> validate_double_column_length(
			const std::vector<double>& values
			, std::string_view name
			, std::size_t expected
		) {
			if (values.size() != expected) {
				return mathfp::unexpected(
					mathfp::invalid_arg("segment column length mismatch")
					.ctx(std::string(kCtxColumn), std::string(name))
					.ctx(std::string(kCtxExpected), static_cast<std::int64_t>(expected))
					.ctx(std::string(kCtxActual), static_cast<std::int64_t>(values.size()))
				);
			}
			return mathfp::kUnit;
		}

		mathfp::Expected<std::size_t> validate_segment_columns(
			const SegmentColumns& columns
		) {
			const auto n = columns.from_zone_id.size();
			if (n == 0) {
				return mathfp::unexpected(
					mathfp::invalid_arg("segment file contains no segments")
				);
			}

			MATHFP_TRY(validate_int_column_length(columns.from_stop_id, "from_stop_id", n));
			MATHFP_TRY(validate_int_column_length(columns.to_zone_id, "to_zone_id", n));
			MATHFP_TRY(validate_int_column_length(columns.to_stop_id, "to_stop_id", n));
			MATHFP_TRY(validate_int_column_length(columns.profile_id, "profile_id", n));
			MATHFP_TRY(validate_int_column_length(columns.trip_id, "trip_id", n));
			MATHFP_TRY(validate_int_column_length(columns.from_index, "from_index", n));
			MATHFP_TRY(validate_int_column_length(columns.to_index, "to_index", n));
			MATHFP_TRY(validate_double_column_length(columns.length_km, "length", n));
			MATHFP_TRY(validate_double_column_length(columns.time_sec, "time", n));
			MATHFP_TRY(validate_double_column_length(columns.dep_sec, "dep", n));
			MATHFP_TRY(validate_double_column_length(columns.arr_sec, "arr", n));
			MATHFP_TRY(validate_double_column_length(columns.fare, "fare", n));

			return n;
		}

		mathfp::Expected<std::unordered_set<std::int64_t>> build_declared_zone_set(
			const std::vector<std::int64_t>& zone_ids
		) {
			std::unordered_set<std::int64_t> zones_set;
			zones_set.reserve(zone_ids.size());
			for (std::size_t i = 0; i < zone_ids.size(); ++i) {
				const auto id = zone_ids[i];
				if (id < 0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("zone_ids must be non-negative")
						.ctx(std::string(kCtxZoneId), id)
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(i))
					);
				}
				if (!zones_set.insert(id).second) {
					return mathfp::unexpected(
						mathfp::invalid_arg("duplicate zone_id")
						.ctx(std::string(kCtxZoneId), id)
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(i))
					);
				}
			}
			return zones_set;
		}

		BuildState make_build_state(
			std::size_t segment_count
			, std::unordered_set<std::int64_t> zones_set
		) {
			BuildState state;
			state.zones_set = std::move(zones_set);
			state.stop_ids.reserve(segment_count / kStopReserveDiv + kReservePadding);
			state.line_ids.reserve(segment_count / kLineReserveDiv + kReservePadding);
			state.extra_zone_ids.reserve(segment_count / kExtraZoneReserveDiv + kReservePadding);
			state.line_routes.reserve(segment_count / kLineReserveDiv + kReservePadding);
			state.route_segments.reserve(segment_count);
			state.connection_segments.reserve(segment_count);
			return state;
		}

		SegmentRowView segment_row_view(
			const SegmentColumns& columns
			, std::size_t index
		) {
			return SegmentRowView{
				.index = index,
				.from_zone = columns.from_zone_id[index],
				.from_stop = columns.from_stop_id[index],
				.to_zone = columns.to_zone_id[index],
				.to_stop = columns.to_stop_id[index],
				.profile = columns.profile_id[index],
				.trip_id = columns.trip_id[index],
				.from_index = columns.from_index[index],
				.to_index = columns.to_index[index],
				.length_km = columns.length_km[index],
				.time_sec = columns.time_sec[index],
				.dep_sec = columns.dep_sec[index],
				.arr_sec = columns.arr_sec[index],
				.fare = columns.fare[index]
			};
		}

		void report_build_progress(
			std::size_t index
			, std::size_t total
			, const BuildStats& stats
		) {
			using timetable::infra::LogLevel;
			using timetable::infra::progress::log;
			using timetable::infra::progress::status;

			if (index == 0 || (index % kProgressStep) != 0) {
				return;
			}

			const auto pct = static_cast<int>(100.0 * static_cast<double>(index) / static_cast<double>(total));
			status(fmt::format("parsing: building segments ({}%)", pct));
			log(
				fmt::format(
					"parsing: progress i={}  walk={}  line={}  timed={}  untimed={}"
					, index
					, stats.walk_segments
					, stats.line_segments
					, stats.timed_segments
					, stats.untimed_segments
				),
				LogLevel::Debug
			);
		}

		mathfp::Expected<mathfp::Unit> ensure_distinct_segment_endpoints(
			const SegmentRowView& row
			, const timetable::domain::WalkEndpoint& from_endpoint
			, const timetable::domain::WalkEndpoint& to_endpoint
		) {
			if (!same_endpoint(from_endpoint, to_endpoint)) {
				return mathfp::kUnit;
			}

			return mathfp::unexpected(
				mathfp::invalid_arg("segment endpoints must be distinct")
				.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
				.ctx(std::string(kCtxFromZoneId), row.from_zone)
				.ctx(std::string(kCtxFromStopId), row.from_stop)
				.ctx(std::string(kCtxToZoneId), row.to_zone)
				.ctx(std::string(kCtxToStopId), row.to_stop)
				.ctx(std::string(kCtxProfileId), row.profile)
				.ctx(std::string(kCtxLength), row.length_km)
				.ctx(std::string(kCtxTime), row.time_sec)
				.ctx(std::string(kCtxDep), row.dep_sec)
				.ctx(std::string(kCtxArr), row.arr_sec)
				.ctx(std::string(kFieldFare), row.fare)
				.ctx(std::string(kCtxTripId), row.trip_id)
				.ctx(std::string(kCtxFromIndex), row.from_index)
				.ctx(std::string(kCtxToIndex), row.to_index)
			);
		}

		bool is_overnight_timed_row(const SegmentRowView& row) {
			return row.profile != kMissingId
				&& row.dep_sec >= 0.0
				&& row.arr_sec >= 0.0
				&& row.arr_sec < row.dep_sec;
		}

		bool should_drop_overnight_row(
			BuildState& state
			, const SegmentRowView& row
		) {
			using timetable::infra::LogLevel;
			using timetable::infra::progress::log;

			if (!is_overnight_timed_row(row)) {
				return false;
			}

			++state.stats.dropped_overnight;
			log(
				fmt::format(
					"parsing: dropping overnight timed segment at row {}  trip_id = {}  line_id = {}  dep = {}  arr = {}"
					, row.index
					, row.trip_id
					, row.profile
					, row.dep_sec
					, row.arr_sec
				),
				LogLevel::Warning
			);
			return true;
		}

		mathfp::Expected<SegmentSemantics> interpret_segment_row(
			const SegmentRowView& row
			, const std::unordered_set<std::int64_t>& zones_set
		) {
			using timetable::domain::Time;

			MATHFP_TRY_LET(
				timetable::domain::WalkEndpoint
				, from_endpoint
				, parse_endpoint(row.from_zone, row.from_stop, kFieldFrom, row.index, zones_set)
			);
			MATHFP_TRY_LET(
				timetable::domain::WalkEndpoint
				, to_endpoint
				, parse_endpoint(row.to_zone, row.to_stop, kFieldTo, row.index, zones_set)
			);
			MATHFP_TRY(ensure_distinct_segment_endpoints(row, from_endpoint, to_endpoint));

			std::optional<Time> dep{};
			std::optional<Time> arr{};
			if (row.dep_sec >= 0.0 || row.arr_sec >= 0.0) {
				if (!(row.dep_sec >= 0.0 && row.arr_sec >= 0.0)) {
					return mathfp::unexpected(
						mathfp::invalid_arg("departure and arrival must both be set or both be -1")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxDep), row.dep_sec)
						.ctx(std::string(kCtxArr), row.arr_sec)
					);
				}
			}

			const auto is_walk_segment = (row.profile == kMissingId);
			if (!is_walk_segment) {
				if (row.profile < 0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("profile_id must be -1 or non-negative")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxProfileId), row.profile)
					);
				}
				if (row.trip_id == kMissingId) {
					return mathfp::unexpected(
						mathfp::invalid_arg("timed line segment must have trip_id")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxLineId), row.profile)
					);
				}
				if (row.trip_id < 0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("trip_id must be -1 or non-negative")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxTripId), row.trip_id)
					);
				}
				if (row.from_index < 0 || row.to_index < 0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("timed line segment must have non-negative stop indices")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxFromIndex), row.from_index)
						.ctx(std::string(kCtxToIndex), row.to_index)
					);
				}
				if (row.to_index <= row.from_index) {
					return mathfp::unexpected(
						mathfp::invalid_arg("to_index must be greater than from_index")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxFromIndex), row.from_index)
						.ctx(std::string(kCtxToIndex), row.to_index)
					);
				}
				MATHFP_TRY(ensure_time_matches_departure_arrival(
					row.time_sec,
					row.dep_sec,
					row.arr_sec,
					row.index
				));
				dep = Time{ row.dep_sec };
				arr = Time{ row.arr_sec };
			} else {
				if (row.dep_sec >= 0.0 || row.arr_sec >= 0.0) {
					return mathfp::unexpected(
						mathfp::invalid_arg("walk segment must not have departure/arrival times")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxDep), row.dep_sec)
						.ctx(std::string(kCtxArr), row.arr_sec)
					);
				}
				if (row.trip_id != kMissingId) {
					return mathfp::unexpected(
						mathfp::invalid_arg("walk segment must have trip_id = -1")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxTripId), row.trip_id)
					);
				}
				if (row.from_index != kMissingId || row.to_index != kMissingId) {
					return mathfp::unexpected(
						mathfp::invalid_arg("walk segment must not carry stop indices")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxFromIndex), row.from_index)
						.ctx(std::string(kCtxToIndex), row.to_index)
					);
				}
			}

			std::optional<double> fare{};
			if (row.fare >= 0.0) {
				fare = row.fare;
			}

			return SegmentSemantics{
				.from_endpoint = std::move(from_endpoint),
				.to_endpoint = std::move(to_endpoint),
				.is_walk_segment = is_walk_segment,
				.dep = std::move(dep),
				.arr = std::move(arr),
				.fare = std::move(fare)
			};
		}

		void collect_model_entities(
			BuildState& state
			, const SegmentRowView& row
			, const SegmentSemantics& semantics
		) {
			using timetable::domain::StopId;
			using timetable::domain::ZoneId;

			if (!semantics.is_walk_segment) {
				state.line_ids.insert(row.profile);
			}

			if (std::holds_alternative<ZoneId>(semantics.from_endpoint)) {
				const auto zid = std::get<ZoneId>(semantics.from_endpoint).get();
				if (!state.zones_set.contains(zid)) {
					state.extra_zone_ids.insert(zid);
				}
			}
			if (std::holds_alternative<ZoneId>(semantics.to_endpoint)) {
				const auto zid = std::get<ZoneId>(semantics.to_endpoint).get();
				if (!state.zones_set.contains(zid)) {
					state.extra_zone_ids.insert(zid);
				}
			}

			if (std::holds_alternative<StopId>(semantics.from_endpoint)) {
				state.stop_ids.insert(std::get<StopId>(semantics.from_endpoint).get());
			}
			if (std::holds_alternative<StopId>(semantics.to_endpoint)) {
				state.stop_ids.insert(std::get<StopId>(semantics.to_endpoint).get());
			}
		}

		mathfp::Expected<const timetable::domain::RouteSegment*> build_route_segment_for_row(
			BuildState& state
			, const SegmentRowView& row
			, const SegmentSemantics& semantics
		) {
			using timetable::domain::Length;
			using timetable::domain::LineId;
			using timetable::domain::RouteSegment;
			using timetable::domain::RouteSegmentId;
			using timetable::domain::SegmentCarrier;
			using timetable::domain::StopId;
			using timetable::domain::Time;
			using timetable::domain::WalkLinkId;
			using timetable::domain::WalkPath;
			using timetable::domain::preprocessing::make_route_segment;

			if (semantics.is_walk_segment) {
				SegmentCarrier carrier = WalkPath{ WalkLinkId{ state.next_walk_id++ } };
				MATHFP_TRY_LET(
					RouteSegment
					, route
					, make_route_segment(
						RouteSegmentId{ state.next_route_segment_id++ }
						, semantics.from_endpoint
						, semantics.to_endpoint
						, Length{ row.length_km }
						, Time{ row.time_sec }
						, std::move(carrier)
					)
				);
				state.route_segments.push_back(std::move(route));
				return &state.route_segments.back();
			}

			if (!std::holds_alternative<StopId>(semantics.from_endpoint)
				|| !std::holds_alternative<StopId>(semantics.to_endpoint)) {
				return mathfp::unexpected(
					mathfp::invalid_arg("line segment endpoints must be stops")
					.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
					.ctx(std::string(kCtxProfileId), row.profile)
				);
			}

			const LineRouteKey key{
				.from = timetable::domain::to_endpoint_key(semantics.from_endpoint),
				.to = timetable::domain::to_endpoint_key(semantics.to_endpoint),
				.line_id = row.profile
			};

			if (const auto it = state.line_routes.find(key); it != state.line_routes.end()) {
				const auto actual_metrics = LineRouteMetrics{
					.length_km = row.length_km,
					.time_sec = row.time_sec
				};
				if (!line_route_metrics_equal(it->second.metrics, actual_metrics)) {
					const auto expected_length = it->second.metrics.length_km;
					const auto expected_time = it->second.metrics.time_sec;
					return mathfp::unexpected(
						mathfp::invalid_arg("inconsistent TIME/LENGTH for identical line stop pair")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxLineId), row.profile)
						.ctx(std::string(kCtxFromStopId), std::get<StopId>(semantics.from_endpoint).get())
						.ctx(std::string(kCtxToStopId), std::get<StopId>(semantics.to_endpoint).get())
						.ctx("expected_length", expected_length)
						.ctx("actual_length", row.length_km)
						.ctx("expected_time", expected_time)
						.ctx("actual_time", row.time_sec)
					);
				}

				const auto route_index = static_cast<std::size_t>(it->second.id.get());
				if (route_index >= state.route_segments.size()) {
					return mathfp::unexpected(
						mathfp::internal_error("missing route segment for deduplicated line route key")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxLineId), row.profile)
					);
				}
				return &state.route_segments[route_index];
			}

			SegmentCarrier carrier = LineId{ row.profile };
			MATHFP_TRY_LET(
				RouteSegment
				, route
				, make_route_segment(
					RouteSegmentId{ state.next_route_segment_id++ }
					, semantics.from_endpoint
					, semantics.to_endpoint
					, Length{ row.length_km }
					, Time{ row.time_sec }
					, std::move(carrier)
				)
			);
			state.line_routes.emplace(
				key,
				LineRouteEntry{
					.id = route.id,
					.metrics = LineRouteMetrics{
						.length_km = row.length_km,
						.time_sec = row.time_sec
					}
				}
			);
			state.route_segments.push_back(std::move(route));
			return &state.route_segments.back();
		}

		mathfp::Expected<mathfp::Unit> append_connection_segment_for_row(
			BuildState& state
			, const SegmentRowView& row
			, const SegmentSemantics& semantics
			, const timetable::domain::RouteSegment& route_segment
		) {
			using timetable::domain::ConnectionSegment;
			using timetable::domain::ConnectionSegmentId;
			using timetable::domain::TripId;
			using timetable::domain::preprocessing::make_connection_segment;

			MATHFP_TRY_LET(
				ConnectionSegment
				, conn
				, make_connection_segment(
					ConnectionSegmentId{ state.next_connection_segment_id++ }
					, route_segment
					, semantics.is_walk_segment ? std::optional<TripId>{} : std::optional<TripId>{ TripId{ row.trip_id } }
					, semantics.is_walk_segment ? std::optional<std::int64_t>{} : std::optional<std::int64_t>{ row.from_index }
					, semantics.is_walk_segment ? std::optional<std::int64_t>{} : std::optional<std::int64_t>{ row.to_index }
					, semantics.dep
					, semantics.arr
					, semantics.fare
				)
			);
			state.connection_segments.push_back(std::move(conn));

			if (semantics.is_walk_segment) {
				++state.stats.walk_segments;
				++state.stats.untimed_segments;
			} else {
				++state.stats.line_segments;
				++state.stats.timed_segments;
			}

			return mathfp::kUnit;
		}

		mathfp::Expected<mathfp::Unit> process_segment_row(
			BuildState& state
			, const SegmentColumns& columns
			, std::size_t index
		) {
			const auto row = segment_row_view(columns, index);
			if (should_drop_overnight_row(state, row)) {
				return mathfp::kUnit;
			}

			MATHFP_TRY_LET(SegmentSemantics, semantics, interpret_segment_row(row, state.zones_set));
			collect_model_entities(state, row, semantics);
			MATHFP_TRY_LET(
				const timetable::domain::RouteSegment*
				, route_segment
				, build_route_segment_for_row(state, row, semantics)
			);
			MATHFP_TRY(append_connection_segment_for_row(state, row, semantics, *route_segment));
			return mathfp::kUnit;
		}

		mathfp::Expected<mathfp::Unit> validate_extra_zones(
			const BuildState& state
			, const ParseParams& params
		) {
			if (state.extra_zone_ids.empty()) {
				return mathfp::kUnit;
			}

			if (!params.allow_unknown_zones) {
				std::vector<std::int64_t> extra_list(state.extra_zone_ids.begin(), state.extra_zone_ids.end());
				std::sort(extra_list.begin(), extra_list.end());
				const auto sample = std::min<std::size_t>(extra_list.size(), 10);
				std::string sample_str;
				for (std::size_t i = 0; i < sample; ++i) {
					if (i > 0) sample_str += ", ";
					sample_str += std::to_string(extra_list[i]);
				}
				return mathfp::unexpected(
					mathfp::invalid_arg("segment zones not listed in zone_ids")
					.ctx(std::string(kCtxCount), static_cast<std::int64_t>(extra_list.size()))
					.ctx(std::string(kCtxSample), sample_str)
				);
			}

			return mathfp::kUnit;
		}

		timetable::domain::InputModel build_input_model(
			const SegmentColumns& columns
			, const BuildState& state
		) {
			using timetable::domain::InputModel;
			using timetable::domain::Line;
			using timetable::domain::LineId;
			using timetable::domain::Stop;
			using timetable::domain::StopId;
			using timetable::domain::Zone;
			using timetable::domain::ZoneId;

			InputModel input;
			input.zones.reserve(columns.zone_ids.size() + state.extra_zone_ids.size());
			for (const auto id : columns.zone_ids) {
				input.zones.push_back(Zone{ ZoneId{ id } });
			}
			if (!state.extra_zone_ids.empty()) {
				std::vector<std::int64_t> extra_list(state.extra_zone_ids.begin(), state.extra_zone_ids.end());
				std::sort(extra_list.begin(), extra_list.end());
				for (const auto id : extra_list) {
					input.zones.push_back(Zone{ ZoneId{ id } });
				}
			}

			input.stops.reserve(state.stop_ids.size());
			std::vector<std::int64_t> stop_list(state.stop_ids.begin(), state.stop_ids.end());
			std::sort(stop_list.begin(), stop_list.end());
			for (const auto id : stop_list) {
				input.stops.push_back(Stop{ StopId{ id }, std::nullopt });
			}

			input.lines.reserve(state.line_ids.size());
			std::vector<std::int64_t> line_list(state.line_ids.begin(), state.line_ids.end());
			std::sort(line_list.begin(), line_list.end());
			for (const auto id : line_list) {
				input.lines.push_back(Line{ LineId{ id } });
			}

			return input;
		}

		timetable::domain::AssignmentInput make_assignment_input(
			timetable::domain::InputModel input
			, BuildState state
		) {
			timetable::domain::AssignmentInput out;
			out.input = std::move(input);
			out.params = timetable::domain::SearchParams{};
			out.presegmented = timetable::domain::AssignmentInput::PresegmentedInput{
				.route_segments = std::move(state.route_segments),
				.connection_segments = std::move(state.connection_segments)
			};
			return out;
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
			, parse_ints(lines[0], kFieldFromZoneId, 1)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, from_stop_id
			, parse_ints(lines[1], kFieldFromStopId, 2)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, to_zone_id
			, parse_ints(lines[2], kFieldToZoneId, 3)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, to_stop_id
			, parse_ints(lines[3], kFieldToStopId, 4)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, profile_id
			, parse_ints(lines[4], kFieldProfileId, 5)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, length_km
			, parse_doubles(lines[5], kFieldLength, 6)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, time_sec
			, parse_doubles(lines[6], kFieldTime, 7)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, dep_sec
			, parse_doubles(lines[7], kFieldDep, 8)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, arr_sec
			, parse_doubles(lines[8], kFieldArr, 9)
		);
		MATHFP_TRY_LET(
			std::vector<double>
			, fare
			, parse_doubles(lines[9], kFieldFare, 10)
		);
		MATHFP_TRY_LET(
			std::vector<std::int64_t>
			, zone_ids
			, parse_ints(lines[10], kFieldZoneIds, 11)
		);

		out.from_zone_id = std::move(from_zone_id);
		out.from_stop_id = std::move(from_stop_id);
		out.to_zone_id = std::move(to_zone_id);
		out.to_stop_id = std::move(to_stop_id);
		out.profile_id = std::move(profile_id);
		// TODO(txt-legacy): this legacy single-file format does not carry
		// TRIP_ID/FROM_INDEX/TO_INDEX. Timed line segments therefore no longer
		// match the stricter trip-aware model and should be either rejected
		// explicitly, the format extended, or this path treated as walk/legacy-only.
		out.trip_id.assign(out.profile_id.size(), kMissingId);
		out.from_index.assign(out.profile_id.size(), kMissingId);
		out.to_index.assign(out.profile_id.size(), kMissingId);
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
		MATHFP_TRY_LET(std::size_t, n, validate_segment_columns(columns));

		log(
			fmt::format(
				"parsing: validated columns; segments = {}"
				, n
			),
			LogLevel::Info
		);
		log("parsing: building zones, stops, lines", LogLevel::Info);
		MATHFP_TRY_LET(std::unordered_set<std::int64_t>, zones_set, build_declared_zone_set(columns.zone_ids));
		auto state = make_build_state(n, std::move(zones_set));

		log(
			fmt::format(
				"parsing: zone_ids = {}"
				, state.zones_set.size()
			),
			LogLevel::Info
		);
		log("parsing: building segments", LogLevel::Info);
		status("parsing: building segments (0%)");
		for (std::size_t i = 0; i < n; ++i) {
			report_build_progress(i, n, state.stats);
			MATHFP_TRY(process_segment_row(state, columns, i));
		}

		status("parsing: building segments (100%)");
		log(
			fmt::format(
				"parsing: segments built; route_segments = {}  connection_segments = {}"
				"  walk = {}  line = {}  timed = {}  untimed = {}  dropped_overnight = {}"
				, state.route_segments.size()
				, state.connection_segments.size()
				, state.stats.walk_segments
				, state.stats.line_segments
				, state.stats.timed_segments
				, state.stats.untimed_segments
				, state.stats.dropped_overnight
			),
			LogLevel::Info
		);
		if (state.stats.dropped_overnight > 0) {
			log(
				fmt::format(
					"parsing: dropped {} overnight timed segments"
					, state.stats.dropped_overnight
				),
				LogLevel::Warning
			);
		}
		MATHFP_TRY(validate_extra_zones(state, params));
		if (!state.extra_zone_ids.empty()) {
			log(
				fmt::format(
					"parsing: zone_ids extended by {} extra zones from segments"
					, state.extra_zone_ids.size()
				),
				LogLevel::Warning
			);
		}

		auto input = build_input_model(columns, state);

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
		log("parsing: assignment input ready", LogLevel::Info);
		status("parsing: done");
		return make_assignment_input(std::move(input), std::move(state));
	}

}  // namespace timetable::infra::txt
