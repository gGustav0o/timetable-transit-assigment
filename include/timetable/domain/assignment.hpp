#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/types/strong_type.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/search/search.hpp"

namespace timetable::domain {

    struct AssignmentInput final {
        struct PresegmentedInput final {
            std::vector<RouteSegment>      route_segments{};
            std::vector<ConnectionSegment> connection_segments{};
        };

        InputModel                       input{};
        SearchParams                     params{};
        std::optional<PresegmentedInput> presegmented{};
    };

    struct AssignmentConnectionRefTag {};

    using AssignmentConnectionRef = mathfp::StrongType<
        std::int64_t
        , AssignmentConnectionRefTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    /**
     * @brief Fully resolved path leg in the final public result.
     *
     * The pair preserves both timetable information (connection segment) and the
     * underlying topology/geometry carrier (route segment).
     */
    struct AssignmentPathSegment final {
        ConnectionSegment connection_segment{};
        RouteSegment      route_segment{};
    };

    /**
     * @brief One chosen connection for an OD pair after the choice step.
     *
     * summary stores the aggregate connection indicators used by search/choice.
     * segments stores the expanded trace in traversal order.
     */
    struct AssignmentConnection final {
        assignment::DiscoveredConnection   summary{};
        std::vector<AssignmentPathSegment> segments{};
    };

    /**
     * @brief Split share assigned to one OD-local chosen connection.
     *
     * connection_index is a zero-based reference into AssignmentOdResult::connections
     * of the same OD result. It is intentionally local to the OD bucket, not a
     * global connection identifier.
     */
    struct AssignmentIntervalShare final {
        AssignmentConnectionRef connection_index{};
        double                  passengers{};
        double                  probability{};
        double                  independence{};
        double                  split_impedance{};
    };

    /**
     * @brief Demand and split outcome for one time interval of an OD pair.
     *
     * demand_passengers is the input demand of the interval.
     * assigned_passengers is the sum of passengers over shares.
     */
    struct AssignmentDemandInterval final {
        TimeInterval                         interval{};
        double                               demand_passengers{};
        double                               assigned_passengers{};
        std::vector<AssignmentIntervalShare> shares{};
    };

    /**
     * @brief Canonical assignment result for one origin-destination pair.
     *
     * connections contains the chosen alternatives after step 3.3.
     * intervals contains the demand/split view from step 3.4.
     * search_connection_count counts all search-stage alternatives before choice.
     * chosen_connection_count equals connections.size().
     *
     * Ordering contract:
     * - od_results are ordered lexicographically by (origin, destination)
     * - connections preserve the choice-stage order within the OD group
     * - intervals preserve the input demand-entry order within the OD group
     */
    struct AssignmentOdResult final {
        ZoneId                                origin{};
        ZoneId                                destination{};
        std::size_t                           search_connection_count{};
        std::size_t                           chosen_connection_count{};
        double                                total_demand_passengers{};
        double                                assigned_passengers{};
        std::vector<AssignmentConnection>     connections{};
        std::vector<AssignmentDemandInterval> intervals{};
    };

    /**
     * @brief Public canonical result of the full timetable assignment pipeline.
     *
     * This is the lossless domain-level result from which UI, text and file
     * projections should be derived.
     */
    struct AssignmentOutput final {
        struct Summary final {
            std::size_t od_count{};
            std::size_t search_connection_count{};
            std::size_t chosen_connection_count{};
            std::size_t demand_share_count{};
            double      total_demand_passengers{};
            double      assigned_passengers{};
        };

        Summary                         summary{};
        std::vector<AssignmentOdResult> od_results{};
    };

}  // namespace timetable::domain
