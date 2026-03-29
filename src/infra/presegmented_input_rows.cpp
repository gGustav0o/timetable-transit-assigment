#include "detail/presegmented_input.hpp"

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/numeric.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::infra::detail::presegmented_input {
	namespace {

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

		[[nodiscard]] bool is_overnight_timed_row(const SegmentRowView& row) {
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
				)
				, LogLevel::Warning
			);
			return state;
		}

	}  // namespace

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
				.stop = std::get<StopId>(from_endpoint)
				, .position = RoutePosition{ row.from_index }
			};
			to_occurrence = StopOccurrence{
				.stop = std::get<StopId>(to_endpoint)
				, .position = RoutePosition{ row.to_index }
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

	mathfp::Expected<BuildState> process_segment_row(
		BuildState state
		, SegmentRowView row
	) {
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

}  // namespace timetable::infra::detail::presegmented_input
