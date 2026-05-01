#pragma once

#include <filesystem>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/capacity.hpp"

namespace timetable::infra::csv {

    /**
     * @brief Parse optional vehicle-journey-item capacity input.
     *
     * Columns:
     * - _VEHJOURNEYID -> TripId
     * - INDEX         -> RoutePosition
     * - TOTALCAP      -> total passenger capacity
     * - SEATCAP       -> seated passenger capacity
     */
    mathfp::Expected<timetable::domain::assignment::VehicleJourneyItemCapacitySet>
    parse_vehicle_journey_item_capacity_csv(
        const std::filesystem::path& path
    );

}  // namespace timetable::infra::csv
