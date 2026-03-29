#include "detail/presegmented_input.hpp"

#include <algorithm>

#include <mathfp/core/error.hpp>

namespace timetable::infra::detail::presegmented_input {

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

}  // namespace timetable::infra::detail::presegmented_input
