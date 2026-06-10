#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/projection/summary.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment::projection {

    enum class AssignmentLoadLevel : std::uint8_t {
          Line
        , Route
        , Trip
        , Segment
    };

    /**
     * @brief One flat row for od_summary.csv.
     *
     * This is a compact analytical representation over one canonical OD result.
     */
    struct AssignmentOdSummaryCsvRow final {
        ZoneId                       origin{};
        ZoneId                       destination{};
        std::size_t                  search_connection_count{};
        std::size_t                  chosen_connection_count{};
        std::size_t                  structural_day_path_count{};
        std::size_t                  timed_support_alternative_count{};
        std::size_t                  interval_admissible_split_alternative_count{};
        std::size_t                  unassigned_demand_count{};
        std::size_t                  interval_count{};
        std::size_t                  share_count{};
        double                       total_demand_passengers{};
        double                       assigned_passengers{};
        double                       unassigned_passengers{};
        std::optional<Time>          fastest_journey_time{};
        std::optional<double>        lowest_fare{};
        std::optional<TransferCount> minimum_transfers{};
    };

    /**
     * @brief One flat row for connections.csv.
     *
     * Rows are indexed by an OD-local AssignmentConnectionRef and therefore join
     * naturally with shares.csv and segments.csv.
     */
    struct AssignmentConnectionCsvRow final {
        ZoneId                  origin{};
        ZoneId                  destination{};
        AssignmentConnectionRef connection_index{};
        Time                    departure{};
        Time                    arrival{};
        Time                    journey_time{};
        Time                    in_vehicle_time{};
        Time                    access_time{};
        Time                    egress_time{};
        Time                    transfer_walk_time{};
        Time                    transfer_wait_time{};
        Time                    transfer_time{};
        TransferCount           transfers{};
        double                  fare{};
        double                  assigned_passengers{};
        std::size_t             share_count{};
        std::size_t             path_segment_count{};
    };

    /**
     * @brief One flat row for shares.csv.
     */
    struct AssignmentShareCsvRow final {
        ZoneId                  origin{};
        ZoneId                  destination{};
        IntervalId              interval_id{};
        Time                    interval_start{};
        Time                    interval_end{};
        double                  interval_demand_passengers{};
        double                  interval_assigned_passengers{};
        AssignmentConnectionRef connection_index{};
        double                  share_passengers{};
        double                  probability{};
        double                  independence{};
        double                  split_impedance{};
    };

    /**
     * @brief One flat row for segments.csv.
     *
     * Each row corresponds to one expanded path segment of one chosen
     * connection.
     */
    struct AssignmentSegmentCsvRow final {
        ZoneId                       origin{};
        ZoneId                       destination{};
        AssignmentConnectionRef      connection_index{};
        std::size_t                  path_segment_index{};
        ConnectionSegmentId          connection_segment_id{};
        RouteSegmentId               route_segment_id{};
        RouteTopologyKind            route_topology_kind{};
        EndpointKind                 physical_from_kind{};
        std::int64_t                 physical_from_id{};
        EndpointKind                 physical_to_kind{};
        std::int64_t                 physical_to_id{};
        Length                       route_length{};
        Time                         route_run_time{};
        std::size_t                  walk_path_link_count{};
        std::optional<LineId>        line_id{};
        std::optional<RouteId>       route_id{};
        std::optional<StopId>        line_from_stop_id{};
        std::optional<RoutePosition> line_from_position{};
        std::optional<StopId>        line_to_stop_id{};
        std::optional<RoutePosition> line_to_position{};
        std::optional<TripId>        trip_id{};
        std::optional<RoutePosition> connection_from_index{};
        std::optional<RoutePosition> connection_to_index{};
        std::optional<Time>          departure{};
        std::optional<Time>          arrival{};
        std::optional<double>        fare{};
    };

    /**
     * @brief One flat row for loads.csv.
     *
     * Segment rows contain primary passenger flow. Line, route and trip rows
     * contain passenger-segment aggregates derived from segment rows.
     */
    struct AssignmentLoadCsvRow final {
        AssignmentLoadLevel                 level{};
        IntervalId                          interval_id{};
        LineId                              line_id{};
        std::optional<RouteId>              route_id{};
        std::optional<TripId>               trip_id{};
        std::optional<RouteSegmentId>       route_segment_id{};
        std::optional<ConnectionSegmentId>  connection_segment_id{};
        std::optional<StopId>               from_stop_id{};
        std::optional<RoutePosition>        from_position{};
        std::optional<StopId>               to_stop_id{};
        std::optional<RoutePosition>        to_position{};
        std::optional<Time>                 departure{};
        std::optional<Time>                 arrival{};
        std::optional<double>               passengers{};
        std::optional<double>               passenger_segments{};
        std::size_t                         segment_load_count{};
    };

    /**
     * @brief One flat row for stop_loads.csv.
     */
    struct AssignmentStopLoadCsvRow final {
        IntervalId interval_id{};
        StopId     stop_id{};
        double     boarding_passengers{};
        double     alighting_passengers{};
        double     transfer_boarding_passengers{};
        double     transfer_alighting_passengers{};
        double     incoming_passenger_segments{};
        double     outgoing_passenger_segments{};
        double     through_passengers{};
        double     stop_turnover_passengers{};
        double     transfer_passengers{};
    };

    /**
     * @brief One flat row for skim_matrix.csv.
     */
    struct AssignmentSkimMatrixCsvRow final {
        ZoneId     origin{};
        ZoneId     destination{};
        IntervalId interval_id{};
        double     demand_passengers{};
        double     assigned_passengers{};
        std::size_t connection_count{};
        std::size_t included_connection_count{};
        Time       journey_time{};
        Time       in_vehicle_time{};
        Time       access_time{};
        Time       egress_time{};
        Time       walk_time{};
        Time       wait_time{};
        Time       transfer_wait_time{};
        Time       transfer_walk_time{};
        double     transfers{};
        double     fare{};
        double     split_impedance{};
    };

    /**
     * @brief One flat row for vehicle_journey_item_loads.csv.
     */
    struct AssignmentVehicleJourneyItemLoadCsvRow final {
        IntervalId                              interval_id{};
        TripId                                  trip_id{};
        RoutePosition                           from_index{};
        double                                  passengers{};
        std::optional<double>                   total_capacity{};
        std::optional<double>                   seat_capacity{};
        std::optional<double>                   load_factor{};
        std::optional<double>                   overload_passengers{};
        VehicleJourneyItemOverloadStatus        status{ VehicleJourneyItemOverloadStatus::MissingCapacity };
    };

    /**
     * @brief One flat row for elementary_segment_loads.csv.
     */
    struct AssignmentElementarySegmentLoadCsvRow final {
        IntervalId    interval_id{};
        TripId        trip_id{};
        RoutePosition from_index{};
        double        passengers{};
    };

    /**
     * @brief One-row execution metadata export.
     */
    struct AssignmentMetadataCsvRow final {
        AssignmentOutputMode       mode{ AssignmentOutputMode::Calculated };
        AssignmentSkimMatrixStatus skim_status{ AssignmentSkimMatrixStatus::DisabledByConfig };
        std::size_t                od_count{};
        std::size_t                search_connection_count{};
        std::size_t                chosen_connection_count{};
        std::size_t                demand_share_count{};
        std::size_t                structural_day_path_count{};
        std::size_t                timed_support_alternative_count{};
        std::size_t                interval_admissible_split_alternative_count{};
        std::size_t                unassigned_demand_count{};
        std::size_t                skim_entry_count{};
        double                     total_demand_passengers{};
        double                     assigned_passengers{};
        double                     unassigned_passengers{};
        std::optional<double>      runtime_seconds{};
    };

    /**
     * @brief Flat analytical export tables derived from AssignmentOutput.
     *
     * The vectors correspond directly to:
     * - od_summary.csv
     * - connections.csv
     * - shares.csv
     * - segments.csv
     * - loads.csv
     * - stop_loads.csv
     * - elementary_segment_loads.csv
     * - skim_matrix.csv
     * - vehicle_journey_item_loads.csv
     * - metadata.csv
     */
    struct AssignmentCsvProjection final {
        std::vector<AssignmentMetadataCsvRow>   metadata_rows{};
        std::vector<AssignmentOdSummaryCsvRow>  od_summary_rows{};
        std::vector<AssignmentConnectionCsvRow> connection_rows{};
        std::vector<AssignmentShareCsvRow>      share_rows{};
        std::vector<AssignmentSegmentCsvRow>    segment_rows{};
        std::vector<AssignmentLoadCsvRow>       load_rows{};
        std::vector<AssignmentStopLoadCsvRow>   stop_load_rows{};
        std::vector<AssignmentElementarySegmentLoadCsvRow> elementary_segment_load_rows{};
        std::vector<AssignmentSkimMatrixCsvRow> skim_matrix_rows{};
        std::vector<AssignmentVehicleJourneyItemLoadCsvRow> vehicle_journey_item_load_rows{};
    };

    mathfp::Expected<AssignmentCsvProjection> build_assignment_csv_projection(
        const AssignmentOutput& output
    );

}  // namespace timetable::domain::assignment::projection
