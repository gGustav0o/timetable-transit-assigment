#include "timetable/infra/presegmented_input.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <boost/container_hash/hash.hpp>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/numeric.hpp"
#include "timetable/domain/preprocessing/segments_factory.hpp"
#include "timetable/domain/state_ops.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra {

	namespace {

		constexpr std::int64_t kMissingId = -1;
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
		constexpr std::string_view kCtxProfileId = "profile_id";
		constexpr std::string_view kCtxSample = "sample";
		constexpr std::string_view kCtxStopId = "stop_id";
		constexpr std::string_view kCtxTime = "time";
		constexpr std::string_view kCtxToIndex = "to_index";
		constexpr std::string_view kCtxToStopId = "to_stop_id";
		constexpr std::string_view kCtxToZoneId = "to_zone_id";
		constexpr std::string_view kCtxTripId = "trip_id";
		constexpr std::string_view kCtxZoneId = "zone_id";

		constexpr std::string_view kFieldFare = "fare";
		constexpr std::string_view kFieldFrom = "from";
		constexpr std::string_view kFieldTo = "to";

		struct LineRouteKey final {
			timetable::domain::StopOccurrenceKey from{};
			timetable::domain::StopOccurrenceKey to{};
			std::int64_t line_id{};

			auto operator<=>(const LineRouteKey&) const = default;
		};

		struct LineRouteKeyHash final {
			std::size_t operator()(const LineRouteKey& key) const noexcept {
				std::size_t seed = 0;
				boost::hash_combine(seed, key.from.stop.get());
				boost::hash_combine(seed, key.from.position.get());
				boost::hash_combine(seed, key.to.stop.get());
				boost::hash_combine(seed, key.to.position.get());
				boost::hash_combine(seed, key.line_id);
				return seed;
			}
		};

		struct LineRouteMetrics final {
			double length_km{};
			double time_sec{};
		};

		struct LineRouteEntry final {
			std::size_t route_segment_index{};
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
			std::optional<timetable::domain::StopOccurrence> from_occurrence{};
			std::optional<timetable::domain::StopOccurrence> to_occurrence{};
			std::optional<timetable::domain::Time> dep{};
			std::optional<timetable::domain::Time> arr{};
			std::optional<double> fare{};
		};

		bool same_endpoint(
			const timetable::domain::WalkEndpoint& a
			, const timetable::domain::WalkEndpoint& b
		) {
			if (a.index() != b.index()) {
				return false;
			}
			if (std::holds_alternative<timetable::domain::StopId>(a)) {
				return std::get<timetable::domain::StopId>(a) == std::get<timetable::domain::StopId>(b);
			}
			return std::get<timetable::domain::ZoneId>(a) == std::get<timetable::domain::ZoneId>(b);
		}

		mathfp::Expected<mathfp::Unit> ensure_time_matches_departure_arrival(
			double time_sec
			, double dep_sec
			, double arr_sec
			, std::size_t row_index
		) {
			const auto scheduled_time = arr_sec - dep_sec;
			if (!mathfp::almost_equal(time_sec, scheduled_time)) {
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
				.index = index
				, .from_zone = columns.from_zone_id[index]
				, .from_stop = columns.from_stop_id[index]
				, .to_zone = columns.to_zone_id[index]
				, .to_stop = columns.to_stop_id[index]
				, .profile = columns.profile_id[index]
				, .trip_id = columns.trip_id[index]
				, .from_index = columns.from_index[index]
				, .to_index = columns.to_index[index]
				, .length_km = columns.length_km[index]
				, .time_sec = columns.time_sec[index]
				, .dep_sec = columns.dep_sec[index]
				, .arr_sec = columns.arr_sec[index]
				, .fare = columns.fare[index]
			};
		}

		mathfp::Expected<mathfp::Unit> report_build_progress(
			std::size_t index
			, std::size_t total
			, const BuildStats& stats
		) {
			using timetable::infra::LogLevel;
			using timetable::infra::progress::log;
			using timetable::infra::progress::status;

			if (index == 0 || (index % kProgressStep) != 0) {
				return mathfp::kUnit;
			}

			const auto pct = static_cast<int>(
				100.0 * static_cast<double>(index) / static_cast<double>(total)
			);
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
			return mathfp::kUnit;
		}

		mathfp::Expected<timetable::domain::WalkEndpoint> parse_endpoint(
			std::int64_t zone_id
			, std::int64_t stop_id
			, std::string_view field
			, std::size_t index
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

		mathfp::Expected<mathfp::Unit> ensure_distinct_walk_segment_endpoints(
			const SegmentRowView& row
			, const timetable::domain::WalkEndpoint& from_endpoint
			, const timetable::domain::WalkEndpoint& to_endpoint
		) {
			if (!same_endpoint(from_endpoint, to_endpoint)) {
				return mathfp::kUnit;
			}

			return mathfp::unexpected(
				mathfp::invalid_arg("walk endpoints must be distinct")
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

		std::optional<BuildState> dropped_overnight_state(
			BuildState state
			, const SegmentRowView& row
		) {
			using timetable::infra::LogLevel;
			using timetable::infra::progress::log;

			if (!is_overnight_timed_row(row)) {
				return std::nullopt;
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
			return state;
		}

		mathfp::Expected<SegmentSemantics> interpret_segment_row(
			const SegmentRowView& row
		) {
			using timetable::domain::RoutePosition;
			using timetable::domain::StopId;
			using timetable::domain::StopOccurrence;
			using timetable::domain::Time;

			MATHFP_TRY_LET(
				timetable::domain::WalkEndpoint
				, from_endpoint
				, parse_endpoint(row.from_zone, row.from_stop, kFieldFrom, row.index)
			);
			MATHFP_TRY_LET(
				timetable::domain::WalkEndpoint
				, to_endpoint
				, parse_endpoint(row.to_zone, row.to_stop, kFieldTo, row.index)
			);

			std::optional<Time> dep{};
			std::optional<Time> arr{};
			std::optional<StopOccurrence> from_occurrence{};
			std::optional<StopOccurrence> to_occurrence{};
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
				if (!std::holds_alternative<StopId>(from_endpoint)
					|| !std::holds_alternative<StopId>(to_endpoint)) {
					return mathfp::unexpected(
						mathfp::invalid_arg("timed line segment endpoints must be stops")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxFromZoneId), row.from_zone)
						.ctx(std::string(kCtxFromStopId), row.from_stop)
						.ctx(std::string(kCtxToZoneId), row.to_zone)
						.ctx(std::string(kCtxToStopId), row.to_stop)
					);
				}
				from_occurrence = StopOccurrence{
					.stop = std::get<StopId>(from_endpoint),
					.position = RoutePosition{ row.from_index }
				};
				to_occurrence = StopOccurrence{
					.stop = std::get<StopId>(to_endpoint),
					.position = RoutePosition{ row.to_index }
				};
				MATHFP_TRY(ensure_time_matches_departure_arrival(
					row.time_sec
					, row.dep_sec
					, row.arr_sec
					, row.index
				));
				dep = Time{ row.dep_sec };
				arr = Time{ row.arr_sec };
			} else {
				MATHFP_TRY(ensure_distinct_walk_segment_endpoints(row, from_endpoint, to_endpoint));
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
				.from_endpoint = std::move(from_endpoint)
				, .to_endpoint = std::move(to_endpoint)
				, .is_walk_segment = is_walk_segment
				, .from_occurrence = std::move(from_occurrence)
				, .to_occurrence = std::move(to_occurrence)
				, .dep = std::move(dep)
				, .arr = std::move(arr)
				, .fare = std::move(fare)
			};
		}

		mathfp::Expected<BuildState> collect_model_entities(
			BuildState state
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
			return state;
		}

		mathfp::Expected<std::size_t> build_route_segment_for_row(
			BuildState& state
			, const SegmentRowView& row
			, const SegmentSemantics& semantics
		) {
			using timetable::domain::Length;
			using timetable::domain::LineId;
			using timetable::domain::RouteSegment;
			using timetable::domain::RouteSegmentId;
			using timetable::domain::RoutePosition;
			using timetable::domain::Time;
			using timetable::domain::WalkLinkId;
			using timetable::domain::WalkPath;
			using timetable::domain::preprocessing::make_route_segment;

			if (semantics.is_walk_segment) {
				MATHFP_TRY_LET(
					RouteSegment
					, route
					, make_route_segment(
						RouteSegmentId{ state.next_route_segment_id++ }
						, semantics.from_endpoint
						, semantics.to_endpoint
						, Length{ row.length_km }
						, Time{ row.time_sec }
						, WalkPath{ WalkLinkId{ state.next_walk_id++ } }
					)
				);
				state.route_segments.push_back(std::move(route));
				return state.route_segments.size() - 1;
			}

			const LineRouteKey key{
				.from = timetable::domain::occurrence_key(*semantics.from_occurrence),
				.to = timetable::domain::occurrence_key(*semantics.to_occurrence),
				.line_id = row.profile
			};

			if (const auto it = state.line_routes.find(key); it != state.line_routes.end()) {
				const auto actual_metrics = LineRouteMetrics{
					.length_km = row.length_km,
					.time_sec = row.time_sec
				};
				if (!line_route_metrics_equal(it->second.metrics, actual_metrics)) {
					return mathfp::unexpected(
						mathfp::invalid_arg("inconsistent TIME/LENGTH for identical line occurrence pair")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxLineId), row.profile)
						.ctx(std::string(kCtxFromStopId), semantics.from_occurrence->stop.get())
						.ctx(std::string(kCtxFromIndex), semantics.from_occurrence->position.get())
						.ctx(std::string(kCtxToStopId), semantics.to_occurrence->stop.get())
						.ctx(std::string(kCtxToIndex), semantics.to_occurrence->position.get())
						.ctx("expected_length", it->second.metrics.length_km)
						.ctx("actual_length", row.length_km)
						.ctx("expected_time", it->second.metrics.time_sec)
						.ctx("actual_time", row.time_sec)
					);
				}

				const auto route_index = it->second.route_segment_index;
				if (route_index >= state.route_segments.size()) {
					return mathfp::unexpected(
						mathfp::internal_error("missing route segment for deduplicated line route key")
						.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
						.ctx(std::string(kCtxLineId), row.profile)
						.ctx("route_segment_index", static_cast<std::int64_t>(route_index))
						.ctx("route_segments_size", static_cast<std::int64_t>(state.route_segments.size()))
					);
				}
				return route_index;
			}

			MATHFP_TRY_LET(
				RouteSegment
				, route
				, make_route_segment(
					RouteSegmentId{ state.next_route_segment_id++ }
					, *semantics.from_occurrence
					, *semantics.to_occurrence
					, Length{ row.length_km }
					, Time{ row.time_sec }
					, LineId{ row.profile }
				)
			);
			state.route_segments.push_back(std::move(route));
			const auto route_segment_index = state.route_segments.size() - 1;
			state.line_routes.emplace(
				key,
				LineRouteEntry{
					.route_segment_index = route_segment_index
					, .metrics = LineRouteMetrics{
						.length_km = row.length_km
						, .time_sec = row.time_sec
					}
				}
			);
			return route_segment_index;
		}

		mathfp::Expected<mathfp::Unit> append_connection_segment_for_row(
			BuildState& state
			, const SegmentRowView& row
			, const SegmentSemantics& semantics
			, std::size_t route_segment_index
		) {
			using timetable::domain::ConnectionSegment;
			using timetable::domain::ConnectionSegmentId;
			using timetable::domain::RouteSegment;
			using timetable::domain::RoutePosition;
			using timetable::domain::TripId;
			using timetable::domain::preprocessing::make_connection_segment;

			if (route_segment_index >= state.route_segments.size()) {
				return mathfp::unexpected(
					mathfp::internal_error("missing route segment while appending connection segment")
					.ctx(std::string(kCtxIndex), static_cast<std::int64_t>(row.index))
					.ctx("route_segment_index", static_cast<std::int64_t>(route_segment_index))
					.ctx("route_segments_size", static_cast<std::int64_t>(state.route_segments.size()))
				);
			}
			const RouteSegment& route_segment = state.route_segments[route_segment_index];

			MATHFP_TRY_LET(
				ConnectionSegment
				, conn
				, make_connection_segment(
					ConnectionSegmentId{ state.next_connection_segment_id++ }
					, route_segment
					, semantics.is_walk_segment ? std::optional<TripId>{} : std::optional<TripId>{ TripId{ row.trip_id } }
					, semantics.is_walk_segment ? std::optional<RoutePosition>{} : std::optional<RoutePosition>{ RoutePosition{ row.from_index } }
					, semantics.is_walk_segment ? std::optional<RoutePosition>{} : std::optional<RoutePosition>{ RoutePosition{ row.to_index } }
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

		mathfp::Expected<BuildState> process_segment_row(
			BuildState state
			, const SegmentColumns& columns
			, std::size_t index
		) {
			const auto row = segment_row_view(columns, index);
			if (const auto dropped = dropped_overnight_state(std::move(state), row); dropped.has_value()) {
				return std::move(*dropped);
			}

			MATHFP_TRY_LET(SegmentSemantics, semantics, interpret_segment_row(row));
			MATHFP_TRY_LET(BuildState, with_entities, collect_model_entities(std::move(state), row, semantics));
			MATHFP_TRY_LET(
				std::size_t
				, route_segment_index
				, build_route_segment_for_row(with_entities, row, semantics)
			);
			MATHFP_TRY(append_connection_segment_for_row(with_entities, row, semantics, route_segment_index));
			return with_entities;
		}

		mathfp::Expected<BuildState> process_segment_row_with_progress(
			BuildState state
			, const SegmentColumns& columns
			, std::size_t count
			, std::size_t index
		) {
			MATHFP_TRY(report_build_progress(index, count, state.stats));
			return process_segment_row(std::move(state), columns, index);
		}

		mathfp::Expected<mathfp::Unit> validate_extra_zones(
			const BuildState& state
			, const PresegmentedInputBuildParams& params
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
					if (i > 0) {
						sample_str += ", ";
					}
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
				.route_segments = std::move(state.route_segments)
				, .connection_segments = std::move(state.connection_segments)
			};
			return out;
		}

	}  // namespace

	mathfp::Expected<timetable::domain::AssignmentInput> build_presegmented_assignment_input(
		SegmentColumns columns
		, const PresegmentedInputBuildParams& params
	) {
		using timetable::infra::LogLevel;
		using timetable::infra::progress::log;
		using timetable::infra::progress::status;

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
		MATHFP_TRY_LET(
			BuildState
			, built_state
			, timetable::domain::state_ops::fold_indexed(
				std::move(state)
				, n
				, [&](BuildState current, std::size_t i) {
					return process_segment_row_with_progress(
						std::move(current)
						, columns
						, n
						, i
					);
				}
			)
		);
		state = std::move(built_state);

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

		status("parsing: validating extra zones");
		MATHFP_TRY(validate_extra_zones(state, params));

		status("parsing: finalizing input model");
		auto input = build_input_model(columns, state);
		log(
			fmt::format(
				"parsing: model entities built; zones = {}  stops = {}  lines = {}"
				, input.zones.size()
				, input.stops.size()
				, input.lines.size()
			),
			LogLevel::Info
		);
		status("parsing: completed");

		return make_assignment_input(std::move(input), std::move(state));
	}

}  // namespace timetable::infra
