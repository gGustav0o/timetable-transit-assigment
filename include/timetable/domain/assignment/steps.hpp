#pragma once

#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/preprocessing/route_segments.hpp"
#include "timetable/domain/preprocessing/connection_segments.hpp"

namespace timetable::domain::assignment {

    struct PreprocessedNetwork final {
        std::vector<RouteSegment>             route_segments{};
        std::vector<ConnectionSegment>        connection_segments{};
        preprocessing::RouteSegmentIndex      route_index{};
        preprocessing::ConnectionSegmentIndex connection_index{};
        double                                fare_scale{ 1.0 };
    };

    struct ConnectionSearchResult final {};

    struct ConnectionChoiceResult final {};

    struct DemandSplitResult final {};

    /**
     * @brief Build route/connection segments and their indices from the input model.
     */
    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network(
        const InputModel& input
        , const PreprocessParams& params
    );

    /**
     * @brief Build indices from presegmented route/connection data.
     *
     * Assumes route/connection segments are already constructed.
     */
    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network_from_segments(
        std::vector<RouteSegment> route_segments
        , std::vector<ConnectionSegment> connection_segments
    );

    /**
     * @brief Compute fare normalization scale from connection segments.
     *
     * Missing fares are ignored. If no fares are present, scale is 1.0.
     */
    double compute_fare_scale(
        std::span<const ConnectionSegment> segments
        , const FareNormalization& normalization
    );

    /**
     * @brief Assign contiguous RouteSegmentId values starting from zero.
     */
    void reindex_route_segments(std::vector<RouteSegment>& segments);

    /**
     * @brief Validate route segments before indexing.
     *
     * Ensures non-empty collection and unique (from, to, carrier) tuples.
     */
    mathfp::Expected<mathfp::Unit> validate_route_segments(
        const std::vector<RouteSegment>& segments
        , bool allow_empty
    );

    /**
     * @brief Enumerate feasible connections using timetable-based branch & bound.
     */
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const PreprocessedNetwork& network
        , const SearchParams& params
    );

    /**
     * @brief Apply choice criteria to remove dominated/illogical connections.
     */
    mathfp::Expected<ConnectionChoiceResult> choose_connections(
        const ConnectionSearchResult& search_result
        , const SearchParams& params
    );

    /**
     * @brief Split OD demand over remaining connections.
     */
    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
        const ConnectionChoiceResult& choice_result
        , const InputModel& input
        , const SearchParams& params
    );

}  // namespace timetable::domain::assignment
