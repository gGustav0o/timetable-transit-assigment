#include "timetable/domain/assignment/steps.hpp"

#include <algorithm>

#include <mathfp/core/error.hpp>
#include <mathfp/core/fp.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/segments_order.hpp"

namespace timetable::domain::assignment {
    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network(
        const InputModel& input
        , const PreprocessParams& params
    ) {
        MATHFP_TRY_LET(
            std::vector<RouteSegment>
            , line_segments
            , preprocessing::build_line_route_segments(
                input.routes, input.trips, input.stops, params
            )
        );
        MATHFP_TRY_LET(
            std::vector<RouteSegment>
            , walk_segments
            , preprocessing::build_walk_route_segments(
                input.walk_links, params
            )
        );

        std::vector<RouteSegment> route_segments;
        route_segments.reserve(line_segments.size() + walk_segments.size());
        route_segments.insert(
            route_segments.end()
            , std::make_move_iterator(line_segments.begin())
            , std::make_move_iterator(line_segments.end())
        );
        route_segments.insert(
            route_segments.end()
            , std::make_move_iterator(walk_segments.begin())
            , std::make_move_iterator(walk_segments.end())
        );
        if (params.stable_ordering) {
            std::sort(route_segments.begin(), route_segments.end(), route_segment_less);
        }
        MATHFP_TRY(validate_route_segments(route_segments, true));

        reindex_route_segments(route_segments);

        MATHFP_TRY_LET(
            std::vector<ConnectionSegment>
            , connection_segments
            , preprocessing::build_connection_segments(
                route_segments, input.routes, input.trips, params
            )
        );
        MATHFP_TRY_LET(
            preprocessing::RouteSegmentIndex
            , route_index
            , preprocessing::build_route_segment_index(route_segments)
        );
        MATHFP_TRY_LET(
            preprocessing::ConnectionSegmentIndex
            , connection_index
            , preprocessing::build_connection_segment_index(
                connection_segments, route_segments
            )
        );

        return PreprocessedNetwork{
            .route_segments        = std::move(route_segments)
            , .connection_segments = std::move(connection_segments)
            , .route_index         = std::move(route_index)
            , .connection_index    = std::move(connection_index)
        };
    }

    void reindex_route_segments(std::vector<RouteSegment>& segments) {
        for (std::size_t i = 0; i < segments.size(); ++i) {
            segments[i].id = RouteSegmentId{ static_cast<std::int64_t>(i) };
        }
    }

    mathfp::Expected<mathfp::Unit> validate_route_segments(
        const std::vector<RouteSegment>& segments
        , bool allow_empty
    ) {
        if (segments.empty() && !allow_empty) {
            return mathfp::unexpected(
                mathfp::invalid_arg("route segments collection is empty")
            );
        }

        struct Key final {
            EndpointKey             from{};
            EndpointKey             to{};
            CarrierKind             kind{};
            std::int64_t            carrier_id{};
            std::vector<WalkLinkId> path{};

            auto operator<=>(const Key&) const = default;
        };

        std::vector<Key> keys;
        keys.reserve(segments.size());
        for (const auto& s : segments) {
            const auto kind = carrier_kind(s.carrier);
            const auto carrier_id = is_line(s.carrier)
                ? std::get<LineId>(s.carrier).get()
                : 0;
            std::vector<WalkLinkId> path{};
            if (is_walk(s.carrier)) {
                path = std::get<WalkPath>(s.carrier);
            }
            keys.push_back(Key{
                .from = to_endpoint_key(s.from),
                .to = to_endpoint_key(s.to),
                .kind = kind,
                .carrier_id = carrier_id,
                .path = std::move(path)
            });
        }

        std::sort(keys.begin(), keys.end());
        const auto dup = std::adjacent_find(keys.begin(), keys.end());
        if (dup != keys.end()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("duplicate route segments detected")
                .ctx("from", dup->from.id)
                .ctx("to", dup->to.id)
                .ctx("carrier_id", dup->carrier_id)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const PreprocessedNetwork& network
        , const SearchParams& params
    ) {
        (void)network;
        (void)params;
        return mathfp::unexpected(
            mathfp::not_implemented("connection search step not implemented yet")
        );
    }

    mathfp::Expected<ConnectionChoiceResult> choose_connections(
        const ConnectionSearchResult& search_result
        , const SearchParams& params
    ) {
        (void)search_result;
        (void)params;
        return mathfp::unexpected(
            mathfp::not_implemented("connection choice step not implemented yet")
        );
    }

    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
        const ConnectionChoiceResult& choice_result
        , const InputModel& input
        , const SearchParams& params
    ) {
        (void)choice_result;
        (void)input;
        (void)params;
        return mathfp::unexpected(
            mathfp::not_implemented("demand split step not implemented yet")
        );
    }

}  // namespace timetable::domain::assignment
