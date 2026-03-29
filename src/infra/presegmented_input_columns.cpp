#include "detail/presegmented_input.hpp"

#include <array>
#include <string>

#include <mathfp/core/error.hpp>
#include <mathfp/core/traverse.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::infra::detail::presegmented_input {
	namespace {

		struct ColumnLengthSpec final {
			std::string_view name{};
			std::size_t actual{};
		};

		mathfp::Expected<mathfp::Unit> validate_column_length(
			const ColumnLengthSpec& spec
			, std::size_t expected
		) {
			if (spec.actual != expected) {
				return mathfp::unexpected(
					mathfp::invalid_arg("segment column length mismatch")
						.ctx(std::string(kCtxColumn), std::string(spec.name))
						.ctx(std::string(kCtxExpected), static_cast<std::int64_t>(expected))
						.ctx(std::string(kCtxActual), static_cast<std::int64_t>(spec.actual))
				);
			}
			return mathfp::kUnit;
		}

		template <std::size_t N>
		mathfp::Expected<mathfp::Unit> validate_column_lengths(
			const std::array<ColumnLengthSpec, N>& specs
			, std::size_t expected
		) {
			MATHFP_TRY_LET(
				std::vector<mathfp::Unit>
				, validated
				, mathfp::trv::traverse(specs, [&](const ColumnLengthSpec& spec) {
					return validate_column_length(spec, expected);
				})
			);
			(void)validated;
			return mathfp::kUnit;
		}

	}  // namespace

	mathfp::Expected<std::size_t> validate_segment_columns(
		const SegmentColumns& columns
	) {
		const auto n = columns.from_zone_id.size();
		if (n == 0) {
			return mathfp::unexpected(
				mathfp::invalid_arg("segment file contains no segments")
			);
		}

		const auto specs = std::array{
			ColumnLengthSpec{ .name = "from_stop_id", .actual = columns.from_stop_id.size() }
			, ColumnLengthSpec{ .name = "to_zone_id", .actual = columns.to_zone_id.size() }
			, ColumnLengthSpec{ .name = "to_stop_id", .actual = columns.to_stop_id.size() }
			, ColumnLengthSpec{ .name = "profile_id", .actual = columns.profile_id.size() }
			, ColumnLengthSpec{ .name = "trip_id", .actual = columns.trip_id.size() }
			, ColumnLengthSpec{ .name = "from_index", .actual = columns.from_index.size() }
			, ColumnLengthSpec{ .name = "to_index", .actual = columns.to_index.size() }
			, ColumnLengthSpec{ .name = "length", .actual = columns.length_km.size() }
			, ColumnLengthSpec{ .name = "time", .actual = columns.time_sec.size() }
			, ColumnLengthSpec{ .name = "dep", .actual = columns.dep_sec.size() }
			, ColumnLengthSpec{ .name = "arr", .actual = columns.arr_sec.size() }
			, ColumnLengthSpec{ .name = "fare", .actual = columns.fare.size() }
		};
		MATHFP_TRY(validate_column_lengths(specs, n));

		return n;
	}

	mathfp::Expected<RawIdSet> build_declared_zone_set(
		const std::vector<std::int64_t>& zone_ids
	) {
		RawIdSet zones_set;
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
		, RawIdSet zones_set
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

}  // namespace timetable::infra::detail::presegmented_input
