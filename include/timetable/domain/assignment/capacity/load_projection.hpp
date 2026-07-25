#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/capacity.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<std::vector<VehicleJourneyItemKey>> vehicle_journey_items_occupied(
          TripId        trip
        , RoutePosition from_index
        , RoutePosition to_index
    );

    [[nodiscard]] mathfp::Expected<std::vector<VehicleJourneyItemKey>> vehicle_journey_items_occupied(
        const ConnectionLeg& leg
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load(
        const VehicleJourneyItemLoad& load
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_loads(
        const VehicleJourneyItemLoads& loads
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load_projection(
          const DemandSplitResult&       split_result
        , const VehicleJourneyItemLoads& loads
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_elementary_segment_loads(
        const ElementarySegmentLoads& loads
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_elementary_segment_load_projection(
          const DemandSplitResult&      split_result
        , const ElementarySegmentLoads& loads
    );

    [[nodiscard]] mathfp::Expected<VehicleJourneyItemLoads> build_vehicle_journey_item_loads(
        const DemandSplitResult& split_result
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentLoads> build_elementary_segment_loads(
        const DemandSplitResult& split_result
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentLoads> build_day_path_elementary_segment_loads(
        const DemandSplitResult& split_result
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> accumulate_elementary_segment_loads(
          ElementarySegmentLoadAccumulator& accumulator
        , const ElementarySegmentLoads&     loads
    );

    [[nodiscard]] mathfp::Expected<ElementarySegmentLoads> materialize_elementary_segment_loads(
        const ElementarySegmentLoadAccumulator& accumulator
    );

}  // namespace timetable::domain::assignment
