#include "timetable/domain/assignment/capacity_aware/load_state.hpp"

#include <utility>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/capacity/load_projection.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load_state(
        const VehicleJourneyItemLoadState& state
    ) {
        return validate_vehicle_journey_item_loads(
            VehicleJourneyItemLoads{
                .items = state.items
            }
        );
    }

    mathfp::Expected<VehicleJourneyItemLoadState> make_vehicle_journey_item_load_state(
        std::vector<VehicleJourneyItemLoad> items
    ) {
        VehicleJourneyItemLoadState state{
            .items = std::move(items)
        };

        MATHFP_TRY(validate_vehicle_journey_item_load_state(state));
        return state;
    }

    mathfp::Expected<VehicleJourneyItemLoadState> make_vehicle_journey_item_load_state(
        const VehicleJourneyItemLoads& loads
    ) {
        return make_vehicle_journey_item_load_state(loads.items);
    }

}  // namespace timetable::domain::assignment
