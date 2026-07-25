#include "timetable/domain/assignment/search_cost.hpp"

#include <utility>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search/cost/capacity_index.hpp"
#include "timetable/domain/assignment/search/cost/validation.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<SearchCapacityCostConfig> make_base_search_capacity_cost_config() {
        SearchCapacityCostConfig config{
              .volume_capacity_ratio = Dimless{ 0.0 }
            , .penalty_policy = CapacityPenaltyPolicy::VolumeCapacityRatio
            , .load_state = VehicleJourneyItemLoadState{}
            , .capacity_set = VehicleJourneyItemCapacitySet{}
            , .index = SearchCapacityCostIndex{}
        };

        MATHFP_TRY(validate_search_capacity_cost_config(
              config
            , SearchCostMode::BaseOnly
        ));
        return config;
    }

    mathfp::Expected<SearchCostContext> make_base_search_cost_context(
          SearchImpedance impedance
        , double          fare_scale
    ) {
        MATHFP_TRY_LET(
              SearchCapacityCostConfig
            , capacity
            , make_base_search_capacity_cost_config()
        );

        impedance.volume_capacity_ratio = Dimless{ 0.0 };
        SearchCostContext context{
              .mode = SearchCostMode::BaseOnly
            , .impedance = std::move(impedance)
            , .fare_scale = fare_scale
            , .capacity = std::move(capacity)
        };

        MATHFP_TRY(validate_search_cost_context(context));
        return context;
    }

    mathfp::Expected<SearchCostContext> make_capacity_aware_search_cost_context(
          SearchImpedance               impedance
        , double                        fare_scale
        , CapacityPenaltyPolicy         penalty_policy
        , VehicleJourneyItemLoadState   load_state
        , VehicleJourneyItemCapacitySet capacity_set
    ) {
        SearchCapacityCostConfig capacity{
              .volume_capacity_ratio = impedance.volume_capacity_ratio
            , .penalty_policy = penalty_policy
            , .load_state = std::move(load_state)
            , .capacity_set = std::move(capacity_set)
        };
        MATHFP_TRY_LET(
              SearchCapacityCostIndex
            , index
            , build_search_capacity_cost_index(
                  capacity.load_state
                , capacity.capacity_set
                , capacity.penalty_policy
            )
        );
        capacity.index = std::move(index);
        SearchCostContext context{
              .mode = SearchCostMode::CapacityAware
            , .impedance = std::move(impedance)
            , .fare_scale = fare_scale
            , .capacity = std::move(capacity)
        };

        MATHFP_TRY(validate_search_cost_context(context));
        return context;
    }

    mathfp::Expected<SearchCostComponents> make_search_cost_components(
          ConnectionImpedanceComponents base
        , CapacityExposure              capacity_exposure
    ) {
        SearchCostComponents components{
              .base = base
            , .capacity_exposure = capacity_exposure
        };

        MATHFP_TRY(validate_search_cost_components(components));
        return components;
    }

}  // namespace timetable::domain::assignment
