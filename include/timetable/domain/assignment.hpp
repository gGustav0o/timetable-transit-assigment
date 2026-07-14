#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "timetable/domain/id.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"
#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/capacity.hpp"
#include "timetable/domain/assignment/capacity_aware_assignment.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/execution_config.hpp"
#include "timetable/domain/assignment/od_day_path_search.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
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
        assignment::SearchExecutionConfig    search_execution{};
        assignment::SearchPruningConfig      search_pruning{};
        assignment::SearchTimeDomainConfig   search_time_domain{};
        assignment::SkimMatrixConfig         skim_matrix{};
        assignment::AssignmentPeriodConfig   assignment_period{};
        assignment::ConnectionDeletionConfig connection_deletion{};
        assignment::DemandSegmentTimeConfig  demand_segment_time{};
        assignment::AssignmentExecutionConfig execution{};
        assignment::CapacityAwareAssignmentConfig capacity_aware_assignment{};
        assignment::VehicleJourneyItemCapacityInput vehicle_journey_item_capacity{};
        std::optional<PresegmentedInput>     presegmented{};
    };

    struct AssignmentConnectionRefTag {};

    using AssignmentConnectionRef = DomainId<AssignmentConnectionRefTag>;

    /**
     * @brief Fully resolved path leg in the final public result.
     *
     * The pair preserves both timetable information (connection segment) and the
     * underlying topology/geometry carrier (route segment).
     */
    struct AssignmentPathSegment final {
        ConnectionSegment connection_segment{};
        RouteSegment      route_segment;
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
        AssignmentConnectionRef connection_index;
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

    struct AssignmentOdSearchDiagnostics final {
        std::size_t alternative_count{};
    };

    struct AssignmentOdChoiceDiagnostics final {
        std::size_t chosen_alternative_count{};
    };

    struct AssignmentOdDiagnostics final {
        AssignmentOdSearchDiagnostics search{};
        AssignmentOdChoiceDiagnostics choice{};
    };

    struct AssignmentOdPaperSplitSummary final {
        std::size_t structural_day_path_count{};
        std::size_t timed_support_alternative_count{};
        std::size_t interval_admissible_split_alternative_count{};
        std::size_t unassigned_demand_count{};
        double      unassigned_passengers{};
    };

    /**
     * @brief Canonical assignment result for one origin-destination pair.
     *
     * connections contains the chosen alternatives after step 3.3.
     * intervals contains the demand/split view from step 3.4.
     * diagnostics contains search/choice counters. search_connection_count and
     * chosen_connection_count are compatibility projections of those diagnostics
     * for existing CSV/text output. In OD-day assignment alternatives are
     * day-level paths, not raw timed timetable connections.
     *
     * Ordering contract:
     * - od_results are ordered lexicographically by (origin, destination)
     * - connections preserve the choice-stage order within the OD group
     * - intervals preserve the input demand-entry order within the OD group
     */
    struct AssignmentOdResult final {
        ZoneId                                origin;
        ZoneId                                destination;
        std::size_t                           search_connection_count{};
        std::size_t                           chosen_connection_count{};
        double                                total_demand_passengers{};
        double                                assigned_passengers{};
        std::vector<AssignmentConnection>     connections{};
        std::vector<AssignmentDemandInterval> intervals{};
        AssignmentOdDiagnostics               diagnostics{};
        AssignmentOdPaperSplitSummary         paper_split{};
    };

    /**
     * @brief Aggregate transit load for one public-transport line in one demand interval.
     *
     * passenger_segments is the sum of segment passenger flows over all loaded
     * ride legs of the line. It is a passenger-segment volume, not a distinct
     * passenger or boarding count.
     */
    struct AssignmentLineLoad final {
        IntervalId  interval;
        LineId      line;
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
        IntervalId  interval;
        LineId      line;
        RouteId     route;
        double      passenger_segments{};
        std::size_t segment_load_count{};
    };

    /**
     * @brief Day-level route aggregate for comparison with VISUM route flows.
     *
     * This projection intentionally has no demand interval key. It sums the
     * route passenger-segment volume over the full assignment day. In OD-day
     * assignment it is derived from day-path split shares and the concrete
     * interval-admissible timed supports selected by the paper-level split;
     * elementary segment loads remain the primary overload/load profile.
     */
    struct AssignmentRouteTotalLoad final {
        LineId      line;
        RouteId     route;
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
        IntervalId  interval;
        LineId      line;
        RouteId     route;
        TripId      trip;
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
        IntervalId          interval;
        LineId              line;
        RouteId             route;
        TripId              trip;
        RouteSegmentId      route_segment;
        ConnectionSegmentId connection_segment;
        StopOccurrence      from;
        StopOccurrence      to;
        Time                departure{};
        Time                arrival{};
        double              passengers{};
    };

    /**
     * @brief Stop-level passenger exchange and pass-through aggregate.
     *
     * boarding/alighting count actual ride-chain entry and exit events.
     * transfer_* count the subset of those events that connects two different
     * ride chains within one passenger connection. incoming/outgoing are
     * passenger-segment flows on ride legs adjacent to the stop. through counts
     * passengers that remain on the same trip through the stop between two
     * adjacent ride legs.
     */
    struct AssignmentStopLoad final {
        IntervalId interval;
        StopId     stop;
        double     boarding_passengers{};
        double     alighting_passengers{};
        double     transfer_boarding_passengers{};
        double     transfer_alighting_passengers{};
        double     incoming_passenger_segments{};
        double     outgoing_passenger_segments{};
        double     through_passengers{};
    };

    /**
     * @brief Day-level stop aggregate for VISUM stop-flow comparison.
     *
     * total_passenger_flow is the undirected through-stop volume used for
     * comparison: boarding + alighting + pass-through occupancy over the whole
     * assignment day. Directional incoming/outgoing segment volumes are kept so
     * the scalar can be audited. In OD-day assignment these aggregates are
     * day-path projections, not raw timed-search alternative counts.
     */
    struct AssignmentStopTotalLoad final {
        StopId stop;
        double boarding_passengers{};
        double alighting_passengers{};
        double incoming_passenger_segments{};
        double outgoing_passenger_segments{};
        double through_passengers{};
        double total_passenger_flow{};
    };

    /**
     * @brief VISUM comparison aggregates derived from the loaded day-path supports.
     *
     * This is not the production load profile of the OD-day formulation.
     * Production loading and overload assessment use elementary_segment_loads;
     * these rows are secondary line/route/stop summaries for comparison and
     * reporting.
     */
    struct AssignmentLoads final {
        std::vector<AssignmentLineLoad>    line_loads{};
        std::vector<AssignmentRouteLoad>   route_loads{};
        std::vector<AssignmentRouteTotalLoad> route_total_loads{};
        std::vector<AssignmentTripLoad>    trip_loads{};
        std::vector<AssignmentSegmentLoad> segment_loads{};
        std::vector<AssignmentStopLoad>    stop_loads{};
        std::vector<AssignmentStopTotalLoad> stop_total_loads{};
    };

    enum class AssignmentOutputMode {
        Calculated,
        AllZoneSearch,
        TimedConnectionDiagnostics,
        AssignmentDisabled
    };

    /**
     * @brief Public canonical result of the full timetable assignment pipeline.
     *
     * This is the lossless domain-level result from which UI, text and file
     * projections should be derived.
     */
    struct AssignmentOutput final {
        struct Diagnostics final {
            std::size_t search_alternative_count{};
            std::size_t chosen_alternative_count{};
        };

        struct Summary final {
            std::size_t od_count{};
            std::size_t search_connection_count{};
            std::size_t chosen_connection_count{};
            std::size_t demand_share_count{};
            std::size_t structural_day_path_count{};
            std::size_t timed_support_alternative_count{};
            std::size_t interval_admissible_split_alternative_count{};
            std::size_t unassigned_demand_count{};
            double      total_demand_passengers{};
            double      assigned_passengers{};
            double      unassigned_passengers{};
            std::optional<double> runtime_seconds{};
            Diagnostics diagnostics{};
        };

        AssignmentOutputMode             mode{ AssignmentOutputMode::Calculated };
        assignment::AssignmentOutputExportProfile export_profile{
            assignment::AssignmentOutputExportProfile::ProductionAggregate
        };
        Summary                          summary{};
        std::vector<AssignmentOdResult>  od_results{};
        AssignmentLoads                  loads{};
        /**
         * Primary load profile for assignment and overload assessment.
         *
         * Each row is an elementary route segment occupied by passengers:
         * interval + trip + half-open stop-position item. Route/stop totals
         * in AssignmentLoads are comparison aggregates derived from split
         * shares; overload is assessed from this elementary profile.
         */
        assignment::ElementarySegmentLoads elementary_segment_loads{};
        assignment::VehicleJourneyItemOverloadAssessment vehicle_journey_item_loads{};
        assignment::AssignmentSkimMatrix skim_matrix{};
        assignment::CapacityAwareAssignmentDiagnostics capacity_aware{};
    };

}  // namespace timetable::domain
