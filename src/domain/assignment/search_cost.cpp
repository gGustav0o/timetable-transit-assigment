#include "timetable/domain/assignment/search_cost.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool finite_nonnegative(
            double value
        ) noexcept {
            return std::isfinite(value) && value >= 0.0;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_finite_nonnegative(
              double      value
            , const char* field
        ) {
            if (finite_nonnegative(value)) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("search cost scalar must be finite and non-negative")
                    .ctx("field", field)
                    .ctx("value", value)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_finite_nonnegative_dimless(
              Dimless     value
            , const char* field
        ) {
            return ensure_finite_nonnegative(
                  mathfp::units::as_dimless(value)
                , field
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_connection_impedance_components(
            const ConnectionImpedanceComponents& components
        ) {
            MATHFP_TRY(ensure_finite_nonnegative(
                  components.in_vehicle_time.value()
                , "in_vehicle_time"
            ));
            MATHFP_TRY(ensure_finite_nonnegative(
                  components.access_time.value()
                , "access_time"
            ));
            MATHFP_TRY(ensure_finite_nonnegative(
                  components.egress_time.value()
                , "egress_time"
            ));
            MATHFP_TRY(ensure_finite_nonnegative(
                  components.transfer_walk_time.value()
                , "transfer_walk_time"
            ));
            MATHFP_TRY(ensure_finite_nonnegative(
                  components.transfer_wait_time.value()
                , "transfer_wait_time"
            ));
            if (components.transfer_count.get() < 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search cost transfer count must be non-negative")
                        .ctx("transfer_count", components.transfer_count.get())
                );
            }
            MATHFP_TRY(ensure_finite_nonnegative(components.fare, "fare"));
            return mathfp::kUnit;
        }

        [[nodiscard]] bool empty_capacity_cost_support(
            const SearchCapacityCostConfig& config
        ) noexcept {
            return config.load_state.items.empty()
                && config.capacity_set.items.empty();
        }

        [[nodiscard]] const VehicleJourneyItemLoad* find_load(
              const VehicleJourneyItemLoadState& state
            , VehicleJourneyItemLoadKey           key
        ) noexcept {
            for (const auto& load : state.items) {
                if (load.key == key) {
                    return &load;
                }
            }
            return nullptr;
        }

        [[nodiscard]] mathfp::Expected<const VehicleJourneyItemCapacity*> find_capacity(
              const VehicleJourneyItemCapacitySet& capacity_set
            , VehicleJourneyItemKey                key
        ) {
            for (const auto& capacity : capacity_set.items) {
                if (capacity.key == key) {
                    return &capacity;
                }
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search cost is missing vehicle journey item capacity")
                    .ctx("trip_id", key.trip.get())
                    .ctx("from_index", key.from_index.get())
            );
        }

        [[nodiscard]] double load_passengers_or_zero(
              const VehicleJourneyItemLoadState& state
            , IntervalId                         interval
            , VehicleJourneyItemKey              item
        ) noexcept {
            const auto* load = find_load(
                  state
                , VehicleJourneyItemLoadKey{
                      .interval = interval
                    , .item = item
                  }
            );
            if (load == nullptr) {
                return 0.0;
            }
            return load->passengers;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_cost_mode(
        SearchCostMode mode
    ) {
        switch (mode) {
            case SearchCostMode::BaseOnly:
            case SearchCostMode::CapacityAware:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(mode))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_search_impedance_weights(
        const SearchImpedance& impedance
    ) {
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.in_vehicle_time
            , "in_vehicle_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.access_time
            , "access_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.egress_time
            , "egress_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.transfer_walk_time
            , "transfer_walk_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.transfer_wait_time
            , "transfer_wait_time"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.transfer_count
            , "transfer_count"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.fare
            , "fare"
        ));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              impedance.volume_capacity_ratio
            , "volume_capacity_ratio"
        ));

        switch (impedance.fare_normalization.kind) {
            case FareNormalization::Kind::None:
            case FareNormalization::Kind::Mean:
            case FareNormalization::Kind::Median:
            case FareNormalization::Kind::P95:
            case FareNormalization::Kind::FixedScale:
                break;
            default:
                return mathfp::unexpected(
                    mathfp::invalid_arg("unknown fare normalization kind")
                        .ctx("kind", static_cast<std::int64_t>(impedance.fare_normalization.kind))
                );
        }

        if (!std::isfinite(impedance.fare_normalization.fixed_scale)
            || impedance.fare_normalization.fixed_scale < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("fare normalization fixed scale must be finite and non-negative")
                    .ctx("fixed_scale", impedance.fare_normalization.fixed_scale)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_capacity_cost_config(
          const SearchCapacityCostConfig& config
        , SearchCostMode                  mode
    ) {
        MATHFP_TRY(validate_search_cost_mode(mode));
        MATHFP_TRY(validate_capacity_penalty_policy(config.penalty_policy));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              config.volume_capacity_ratio
            , "volume_capacity_ratio"
        ));

        switch (mode) {
            case SearchCostMode::BaseOnly:
                if (mathfp::units::as_dimless(config.volume_capacity_ratio) == 0.0
                    && empty_capacity_cost_support(config)) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("base-only search cost context must not carry active capacity-cost data")
                        .ctx(
                              "volume_capacity_ratio"
                            , mathfp::units::as_dimless(config.volume_capacity_ratio)
                        )
                        .ctx("load_state_items", static_cast<std::int64_t>(config.load_state.items.size()))
                        .ctx("capacity_items", static_cast<std::int64_t>(config.capacity_set.items.size()))
                );

            case SearchCostMode::CapacityAware:
                if (!(mathfp::units::as_dimless(config.volume_capacity_ratio) > 0.0)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search cost requires positive SearchImp.volCapRatioFactor")
                            .ctx(
                                  "volume_capacity_ratio"
                                , mathfp::units::as_dimless(config.volume_capacity_ratio)
                            )
                    );
                }
                if (config.capacity_set.items.empty()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search cost requires vehicle journey item capacity input")
                    );
                }
                MATHFP_TRY(validate_vehicle_journey_item_load_state(config.load_state));
                MATHFP_TRY(validate_vehicle_journey_item_capacity_set(config.capacity_set));
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(mode))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_search_cost_context(
        const SearchCostContext& context
    ) {
        MATHFP_TRY(validate_search_cost_mode(context.mode));
        MATHFP_TRY(validate_search_impedance_weights(context.impedance));
        MATHFP_TRY(ensure_finite_nonnegative(context.fare_scale, "fare_scale"));
        MATHFP_TRY(validate_search_capacity_cost_config(
              context.capacity
            , context.mode
        ));
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_search_cost_components(
        const SearchCostComponents& components
    ) {
        MATHFP_TRY(validate_connection_impedance_components(components.base));
        MATHFP_TRY(validate_capacity_exposure(components.capacity_exposure));
        return mathfp::kUnit;
    }

    mathfp::Expected<SearchCapacityCostConfig> make_base_search_capacity_cost_config() {
        SearchCapacityCostConfig config{
              .volume_capacity_ratio = Dimless{ 0.0 }
            , .penalty_policy = CapacityPenaltyPolicy::VolumeCapacityRatio
            , .load_state = VehicleJourneyItemLoadState{}
            , .capacity_set = VehicleJourneyItemCapacitySet{}
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

    mathfp::Expected<double> base_search_impedance(
          const ConnectionImpedanceComponents& components
        , const SearchImpedance&               impedance
        , double                               fare_scale
    ) {
        MATHFP_TRY(validate_connection_impedance_components(components));
        MATHFP_TRY(validate_search_impedance_weights(impedance));
        MATHFP_TRY(ensure_finite_nonnegative(fare_scale, "fare_scale"));

        const auto value = connection_impedance_value(
              components
            , impedance
            , fare_scale
        );
        MATHFP_TRY(ensure_finite_nonnegative(value, "base_search_impedance"));
        return value;
    }

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const Connection&               connection
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        MATHFP_TRY(validate_search_capacity_cost_config(
              capacity
            , SearchCostMode::CapacityAware
        ));
        return connection_capacity_exposure(
              connection
            , interval
            , capacity.load_state
            , capacity.capacity_set
            , capacity.penalty_policy
        );
    }

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const ConnectionLeg&            ride_leg
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        MATHFP_TRY(validate_search_capacity_cost_config(
              capacity
            , SearchCostMode::CapacityAware
        ));
        if (!is_ride_leg(ride_leg.kind)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search exposure can only be computed for ride legs")
                    .ctx("leg_kind", std::string(to_string(ride_leg.kind)))
            );
        }

        MATHFP_TRY_LET(
              std::vector<VehicleJourneyItemKey>
            , occupied_items
            , vehicle_journey_items_occupied(ride_leg)
        );

        const auto duration = ride_leg.end_time.value() - ride_leg.start_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(duration, "ride_leg_duration"));
        const auto item_duration = duration
            / static_cast<double>(occupied_items.size());

        mathfp::CompensatedSum<double> exposure_seconds;
        for (const auto& item : occupied_items) {
            MATHFP_TRY_LET(
                  const VehicleJourneyItemCapacity*
                , item_capacity
                , find_capacity(capacity.capacity_set, item)
            );
            MATHFP_TRY_LET(
                  Dimless
                , ratio
                , capacity_ratio(
                      load_passengers_or_zero(
                            capacity.load_state
                          , interval
                          , item
                      )
                    , *item_capacity
                  )
            );
            MATHFP_TRY_LET(
                  Dimless
                , penalty
                , capacity_penalty(capacity.penalty_policy, ratio)
            );
            exposure_seconds.add(item_duration * mathfp::units::as_dimless(penalty));
        }

        return make_capacity_exposure(Time{ exposure_seconds.value() });
    }

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const SearchConnection&         connection
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        MATHFP_TRY(validate_search_capacity_cost_config(
              capacity
            , SearchCostMode::CapacityAware
        ));
        return connection_capacity_exposure(
              connection
            , interval
            , capacity.load_state
            , capacity.capacity_set
            , capacity.penalty_policy
        );
    }

    mathfp::Expected<double> capacity_adjusted_search_impedance(
          double           base_impedance
        , CapacityExposure exposure
        , Dimless          volume_capacity_ratio
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(
              base_impedance
            , "base_search_impedance"
        ));
        MATHFP_TRY(validate_capacity_exposure(exposure));
        MATHFP_TRY(ensure_finite_nonnegative_dimless(
              volume_capacity_ratio
            , "volume_capacity_ratio"
        ));

        const auto adjusted = base_impedance
            + mathfp::units::as_dimless(volume_capacity_ratio)
                * exposure.equivalent_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(
              adjusted
            , "capacity_adjusted_search_impedance"
        ));
        return adjusted;
    }

    mathfp::Expected<double> search_impedance(
          const SearchCostComponents& components
        , const SearchCostContext&    context
    ) {
        MATHFP_TRY(validate_search_cost_components(components));
        MATHFP_TRY(validate_search_cost_context(context));

        MATHFP_TRY_LET(
              double
            , base
            , base_search_impedance(
                  components.base
                , context.impedance
                , context.fare_scale
            )
        );

        switch (context.mode) {
            case SearchCostMode::BaseOnly:
                return base;

            case SearchCostMode::CapacityAware:
                return capacity_adjusted_search_impedance(
                      base
                    , components.capacity_exposure
                    , context.capacity.volume_capacity_ratio
                );
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(context.mode))
        );
    }

}  // namespace timetable::domain::assignment
