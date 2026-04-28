#pragma once

#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"

namespace timetable::domain::assignment {

    struct PreprocessedNetwork final {
        std::vector<RouteSegment>             route_segments{};
        std::vector<ConnectionSegment>        connection_segments{};
        preprocessing::RouteSegmentIndex      route_index{};
        preprocessing::ConnectionSegmentIndex connection_index{};
    };

    /**
     * @brief Build route/connection segments and their indices from the input model.
     */
    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network(
          const InputModel&         input
        , const PreprocessParams& params
    );

    /**
     * @brief Build indices from presegmented route/connection data.
     *
     * Assumes route/connection segments are already constructed.
     */
    mathfp::Expected<PreprocessedNetwork> build_preprocessed_network_from_segments(
          std::vector<RouteSegment>        route_segments
        , std::vector<ConnectionSegment> connection_segments
    );

    /**
     * @brief Compute fare normalization scale from connection segments.
     *
     * Missing and zero fares are ignored when deriving the positive scale.
     * Zero fare is a valid free-ride value, but it cannot define a
     * normalization denominator. If no positive fares are present, scale is 1.0.
     */
    double compute_fare_scale(
          std::span<const ConnectionSegment> segments
        , const FareNormalization&         normalization
    );

    /**
     * @brief Assign contiguous RouteSegmentId values starting from zero.
     */
    std::vector<RouteSegment> reindex_route_segments(std::vector<RouteSegment> segments);

    /**
     * @brief Validate route segments before indexing.
     *
     * Ensures non-empty collection and unique route-topology tuples.
     */
    mathfp::Expected<mathfp::Unit> validate_route_segments(
          const std::vector<RouteSegment>& segments
        , bool                           allow_empty
    );

    /**
     * @brief Validate connection segments against canonical route segments.
     *
     * Ensures non-empty collection, valid route references, unique ids and
     * factory-level invariants for each connection segment.
     */
    mathfp::Expected<mathfp::Unit> validate_connection_segments(
          const std::vector<ConnectionSegment>& segments
        , std::span<const RouteSegment>       route_segments
        , bool                                allow_empty
    );

}  // namespace timetable::domain::assignment
