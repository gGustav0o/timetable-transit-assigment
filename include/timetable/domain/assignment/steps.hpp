#pragma once

#include <optional>
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
    };

    struct ConnectionSearchResult final {};

    struct ConnectionChoiceResult final {};

    struct DemandSplitResult final {};

    struct BranchState final {
        std::optional<Time>             start_time{};
        std::optional<Time>             current_arrival_time{};
        const ConnectionSegment*        last_segment{};
        std::optional<TransferCount>    transfer_count{};
    };

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
    std::vector<RouteSegment> reindex_route_segments(std::vector<RouteSegment> segments);

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
     * @brief Search-level feasibility predicate for extending a connection branch.
     *
     * Enforces temporal suitability, start-wait policy, and forbids transfers
     * to the same TRIP_ID. The candidate is interpreted relative to the full
     * current branch state, not only to one predecessor segment.
     */
    bool is_branch_extension_feasible(
        const BranchState& state
        , const ConnectionSegment& candidate
        , const TransferLimits& limits
    ) noexcept;

    /**
     * @brief Enumerate feasible connections using timetable-based branch & bound.
     */
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const PreprocessedNetwork& network
        , double fare_scale
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
