#pragma once

#include <string>

#include "timetable/domain/assignment/projection/csv.hpp"

namespace timetable::infra {

    std::string serialize_assignment_metadata_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_od_summary_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_connections_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_shares_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_segments_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_loads_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_stop_loads_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_skim_matrix_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

    std::string serialize_assignment_vehicle_journey_item_loads_csv(
        const timetable::domain::assignment::projection::AssignmentCsvProjection& projection
    );

}  // namespace timetable::infra
