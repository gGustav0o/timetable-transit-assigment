#include "detail/output_internal.hpp"

#include <cstddef>

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment::detail {

	mathfp::Expected<AssignmentConnection> build_assignment_connection(
		const DiscoveredConnection& connection
		, const PreprocessedNetwork& network
	) {
		AssignmentConnection output{
			.summary = connection
			, .segments = {}
		};
		output.segments.reserve(connection.segments.size());

		for (const auto segment_id : connection.segments) {
			const auto connection_segment_index = static_cast<std::size_t>(segment_id.get());
			if (connection_segment_index >= network.connection_segments.size()) {
				return mathfp::unexpected(
					mathfp::internal_error("assignment output mapping references unknown connection segment")
						.ctx("connection_segment_id", segment_id.get())
						.ctx("connection_origin", connection.origin.get())
						.ctx("connection_destination", connection.destination.get())
				);
			}

			const auto& connection_segment = network.connection_segments[connection_segment_index];
			const auto route_segment_index = static_cast<std::size_t>(connection_segment.route_segment.get());
			if (route_segment_index >= network.route_segments.size()) {
				return mathfp::unexpected(
					mathfp::internal_error("assignment output mapping references unknown route segment")
						.ctx("route_segment_id", connection_segment.route_segment.get())
						.ctx("connection_segment_id", connection_segment.id.get())
				);
			}

			output.segments.push_back(
				AssignmentPathSegment{
					.connection_segment = connection_segment
					, .route_segment = network.route_segments[route_segment_index]
				}
			);
		}

		return output;
	}

}  // namespace timetable::domain::assignment::detail
