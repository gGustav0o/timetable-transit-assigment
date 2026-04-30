#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/types/strong_type.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"
#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/execution_config.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/assignment/search_pruning_config.hpp"
#include "timetable/domain/assignment/search_time_domain_config.hpp"
#include "timetable/domain/assignment/skim.hpp"
#include "timetable/domain/assignment/skim_config.hpp"

namespace timetable::domain {

    struct AssignmentInput final {
        struct PresegmentedInput final {
            std::vector<RouteSegment>      route_segments{};
            std::vector<ConnectionSegment> connection_segments{};
        };

        InputModel                         input{};
        SearchParams                       params{};
        assignment::ChoiceConfig             choice{};
        assignment::CompleteConnectionDominanceConfig complete_connection_dominance{};
        assignment::SearchPruningConfig      search_pruning{};
        assignment::SearchTimeDomainConfig   search_time_domain{};
        assignment::SkimMatrixConfig         skim_matrix{};
        assignment::AssignmentPeriodConfig   assignment_period{};
        assignment::ConnectionDeletionConfig connection_deletion{};
        assignment::DemandSegmentTimeConfig  demand_segment_time{};
        assignment::AssignmentExecutionConfig execution{};
        std::optional<PresegmentedInput>     presegmented{};
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
     * summary stores the strict canonical connection selected by search/choice.
     * segments stores the expanded supply-backed path projection in traversal order.
     */
    struct AssignmentConnection final {
        assignment::SearchConnection      summary;
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
     * @brief Aggregate transit load for one public-transport line in one demand interval.
     *
     * passenger_segments is the sum of segment passenger flows over all loaded
     * ride legs of the line. It is a passenger-segment volume, not a distinct
     * passenger or boarding count.
     */
    struct AssignmentLineLoad final {
        IntervalId  interval{};
        LineId      line{};
        double      passenger_segments{};
        std::size_t segment_load_count{};
    };

    /**
     * @brief Aggregate transit load for one route pattern in one demand interval.
     *
     * passenger_segments is derived from primary segment loads and must not be
     * interpreted as distinct passenger count.
     */
    struct AssignmentRouteLoad final {
        IntervalId  interval{};
        LineId      line{};
        RouteId     route{};
        double      passenger_segments{};
        std::size_t segment_load_count{};
    };

    /**
     * @brief Aggregate transit load for one concrete trip in one demand interval.
     *
     * passenger_segments is derived from segment loads for the trip. The segment
     * profile remains the primary load representation.
     */
    struct AssignmentTripLoad final {
        IntervalId  interval{};
        LineId      line{};
        RouteId     route{};
        TripId      trip{};
        double      passenger_segments{};
        std::size_t segment_load_count{};
    };

    /**
     * @brief Primary transit load on one time-realized route segment.
     *
     * The key is interval + line + trip + route segment + connection segment.
     * Stop occurrences are included so loop lines retain their route-position
     * semantics in downstream projections.
     */
    struct AssignmentSegmentLoad final {
        IntervalId          interval{};
        LineId              line{};
        RouteId             route{};
        TripId              trip{};
        RouteSegmentId      route_segment{};
        ConnectionSegmentId connection_segment{};
        StopOccurrence      from{};
        StopOccurrence      to{};
        Time                departure{};
        Time                arrival{};
        double              passengers{};
    };

    /**
     * @brief Demand-induced public-transport loads derived from split shares.
     */
    struct AssignmentLoads final {
        std::vector<AssignmentLineLoad>    line_loads{};
        std::vector<AssignmentRouteLoad>   route_loads{};
        std::vector<AssignmentTripLoad>    trip_loads{};
        std::vector<AssignmentSegmentLoad> segment_loads{};
    };

    enum class AssignmentOutputMode {
        Calculated,
        AssignmentDisabled
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
            std::optional<double> runtime_seconds{};
        };

        AssignmentOutputMode             mode{ AssignmentOutputMode::Calculated };
        Summary                          summary{};
        std::vector<AssignmentOdResult>  od_results{};
        AssignmentLoads                  loads{};
        assignment::AssignmentSkimMatrix skim_matrix{};
    };

}  // namespace timetable::domain
