#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::domain::assignment::projection {

    /**
     * @brief Human-oriented compact view of one chosen connection.
     *
     * This is a derived representation over AssignmentOutput, not a canonical
     * result. assigned_passengers aggregates all interval shares that reference
     * this OD-local chosen connection.
     */
    struct AssignmentConnectionSummary final {
        AssignmentConnectionRef index{};
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
    };

    /**
     * @brief Compact per-OD summary derived from the canonical result.
     */
    struct AssignmentOdSummary final {
        ZoneId                                   origin{};
        ZoneId                                   destination{};
        std::size_t                              search_connection_count{};
        std::size_t                              chosen_connection_count{};
        std::size_t                              interval_count{};
        std::size_t                              share_count{};
        double                                   total_demand_passengers{};
        double                                   assigned_passengers{};
        std::optional<Time>                      fastest_journey_time{};
        std::optional<double>                    lowest_fare{};
        std::optional<TransferCount>             minimum_transfers{};
        std::vector<AssignmentConnectionSummary> connections{};
    };

    /**
     * @brief Derived summary representation of AssignmentOutput.
     *
     * This representation is intended for UI/text/export projections and keeps
     * only compact aggregate information. The canonical domain result remains
     * AssignmentOutput.
     *
     * time_interval_count counts distinct input time interval identifiers.
     * task_count counts OD-interval demand tasks present in the output.
     */
    struct AssignmentResultSummary final {
        AssignmentOutputMode             mode{ AssignmentOutputMode::Calculated };
        AssignmentOutput::Summary        totals{};
        std::size_t                      time_interval_count{};
        std::size_t                      task_count{};
        std::size_t                      nonempty_od_count{};
        std::size_t                      line_load_count{};
        std::size_t                      route_load_count{};
        std::size_t                      trip_load_count{};
        std::size_t                      segment_load_count{};
        std::size_t                      skim_entry_count{};
        AssignmentSkimMatrixStatus       skim_status{ AssignmentSkimMatrixStatus::DisabledByConfig };
        std::vector<AssignmentOdSummary> od_results{};
    };

    mathfp::Expected<AssignmentResultSummary> build_assignment_result_summary(
        const AssignmentOutput& output
    );

}  // namespace timetable::domain::assignment::projection
