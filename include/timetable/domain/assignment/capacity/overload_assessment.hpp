#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/capacity.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] VehicleJourneyItemOverloadAssessment
    make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment();

    [[nodiscard]] VehicleJourneyItemOverloadAssessment
    make_disabled_by_config_vehicle_journey_item_overload_assessment();

    [[nodiscard]] VehicleJourneyItemOverloadAssessment
    make_missing_capacity_input_vehicle_journey_item_overload_assessment();

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemOverloadAssessment>
    assess_vehicle_journey_item_overload(
          const VehicleJourneyItemLoads&       loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentOverloadAssessment>
    assess_elementary_segment_overload(
          const ElementarySegmentLoads&        loads
        , const VehicleJourneyItemCapacitySet& capacities
        , const std::vector<TimeInterval>&     intervals
    );

}  // namespace timetable::domain::assignment
