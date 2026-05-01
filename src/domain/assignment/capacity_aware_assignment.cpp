#include "timetable/domain/assignment/capacity_aware_assignment.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
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
                mathfp::invalid_arg("capacity-aware assignment scalar must be finite and non-negative")
                    .ctx("field", field)
                    .ctx("value", value)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_positive_capacity(
            const VehicleJourneyItemCapacity& capacity
        ) {
            if (capacity.total_capacity > 0.0) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment requires positive total capacity")
                    .ctx("trip_id", capacity.key.trip.get())
                    .ctx("from_index", capacity.key.from_index.get())
                    .ctx("total_capacity", capacity.total_capacity)
            );
        }

        using LoadLookup = std::map<VehicleJourneyItemLoadKey, const VehicleJourneyItemLoad*>;
        using CapacityLookup = std::map<VehicleJourneyItemKey, const VehicleJourneyItemCapacity*>;

        [[nodiscard]] LoadLookup build_load_lookup(
            const VehicleJourneyItemLoadState& state
        ) {
            LoadLookup lookup;
            for (const auto& load : state.items) {
                lookup.emplace(load.key, &load);
            }
            return lookup;
        }

        [[nodiscard]] CapacityLookup build_capacity_lookup(
            const VehicleJourneyItemCapacitySet& capacity_set
        ) {
            CapacityLookup lookup;
            for (const auto& capacity : capacity_set.items) {
                lookup.emplace(capacity.key, &capacity);
            }
            return lookup;
        }

        [[nodiscard]] double load_for_item(
              const LoadLookup&        lookup
            , IntervalId               interval
            , VehicleJourneyItemKey    item
        ) noexcept {
            const auto it = lookup.find(
                VehicleJourneyItemLoadKey{
                      .interval = interval
                    , .item     = item
                }
            );
            if (it == lookup.end()) {
                return 0.0;
            }
            return it->second->passengers;
        }

        [[nodiscard]] mathfp::Expected<const VehicleJourneyItemCapacity*> capacity_for_item(
              const CapacityLookup&     lookup
            , VehicleJourneyItemKey     item
        ) {
            const auto it = lookup.find(item);
            if (it != lookup.end()) {
                return it->second;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment is missing vehicle journey item capacity")
                    .ctx("trip_id", item.trip.get())
                    .ctx("from_index", item.from_index.get())
            );
        }

        [[nodiscard]] mathfp::Expected<double> ride_leg_duration_seconds(
            const ConnectionLeg& leg
        ) {
            const auto duration = leg.end_time.value() - leg.start_time.value();
            if (finite_nonnegative(duration)) {
                return duration;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity exposure cannot be computed from a negative or non-finite ride duration")
                    .ctx("start_time", leg.start_time.value())
                    .ctx("end_time", leg.end_time.value())
                    .ctx("duration", duration)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> accumulate_ride_leg_capacity_exposure(
              mathfp::CompensatedSum<double>& exposure_seconds
            , const ConnectionLeg&            leg
            , IntervalId                       interval
            , const LoadLookup&                load_lookup
            , const CapacityLookup&            capacity_lookup
            , CapacityPenaltyPolicy            policy
        ) {
            MATHFP_TRY_LET(
                  std::vector<VehicleJourneyItemKey>
                , occupied_items
                , vehicle_journey_items_occupied(leg)
            );
            MATHFP_TRY_LET(
                  double
                , leg_duration
                , ride_leg_duration_seconds(leg)
            );

            const auto item_duration = leg_duration
                / static_cast<double>(occupied_items.size());

            for (const auto& item : occupied_items) {
                MATHFP_TRY_LET(
                      const VehicleJourneyItemCapacity*
                    , capacity
                    , capacity_for_item(capacity_lookup, item)
                );
                MATHFP_TRY_LET(
                      Dimless
                    , ratio
                    , capacity_ratio(
                          load_for_item(load_lookup, interval, item)
                        , *capacity
                      )
                );
                MATHFP_TRY_LET(
                      Dimless
                    , penalty
                    , capacity_penalty(policy, ratio)
                );
                exposure_seconds.add(
                    item_duration * mathfp::units::as_dimless(penalty)
                );
            }

            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_capacity_penalty_policy(
        CapacityPenaltyPolicy policy
    ) {
        switch (policy) {
            case CapacityPenaltyPolicy::VolumeCapacityRatio:
            case CapacityPenaltyPolicy::ExcessVolumeCapacityRatio:
                return mathfp::kUnit;
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown capacity penalty policy")
                .ctx("policy", static_cast<std::int64_t>(policy))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_iteration_config(
        const CapacityIterationConfig& config
    ) {
        if (config.max_iterations <= 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split max_iterations must be positive")
                    .ctx("max_iterations", config.max_iterations)
            );
        }

        MATHFP_TRY(ensure_finite_nonnegative(
              config.absolute_load_tolerance
            , "absolute_load_tolerance"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              config.relative_load_tolerance
            , "relative_load_tolerance"
        ));

        if (config.absolute_load_tolerance == 0.0 && config.relative_load_tolerance == 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split requires at least one positive convergence tolerance")
                    .ctx("absolute_load_tolerance", config.absolute_load_tolerance)
                    .ctx("relative_load_tolerance", config.relative_load_tolerance)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_config(
        const CapacityAwareAssignmentConfig& config
    ) {
        MATHFP_TRY(validate_capacity_penalty_policy(config.penalty_policy));
        MATHFP_TRY(validate_capacity_iteration_config(config.iteration));

        if (config.capacity_aware_search_enabled) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search is a separate future layer and is not implemented yet")
                    .ctx("capacity_aware_split_enabled", config.capacity_aware_split_enabled ? "true" : "false")
                    .ctx("capacity_aware_search_enabled", "true")
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_vehicle_journey_item_load_state(
        const VehicleJourneyItemLoadState& state
    ) {
        return validate_vehicle_journey_item_loads(
            VehicleJourneyItemLoads{
                .items = state.items
            }
        );
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_exposure(
        CapacityExposure exposure
    ) {
        return ensure_finite_nonnegative(
              exposure.equivalent_time.value()
            , "equivalent_time"
        );
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_split_diagnostics(
        const CapacityAwareSplitDiagnostics& diagnostics
    ) {
        if (diagnostics.iterations < 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware split diagnostics iterations must be non-negative")
                    .ctx("iterations", diagnostics.iterations)
            );
        }

        MATHFP_TRY(ensure_finite_nonnegative(
              diagnostics.max_load_delta
            , "max_load_delta"
        ));
        MATHFP_TRY(ensure_finite_nonnegative(
              diagnostics.max_relative_load_delta
            , "max_relative_load_delta"
        ));

        if (!diagnostics.enabled) {
            if (
                   diagnostics.iterations == 0
                && !diagnostics.converged
                && diagnostics.max_load_delta == 0.0
                && diagnostics.max_relative_load_delta == 0.0
            ) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("disabled capacity-aware split diagnostics must be empty")
                    .ctx("iterations", diagnostics.iterations)
                    .ctx("converged", diagnostics.converged ? "true" : "false")
                    .ctx("max_load_delta", diagnostics.max_load_delta)
                    .ctx("max_relative_load_delta", diagnostics.max_relative_load_delta)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_capacity_aware_assignment_diagnostics(
        const CapacityAwareAssignmentDiagnostics& diagnostics
    ) {
        MATHFP_TRY(validate_capacity_penalty_policy(diagnostics.penalty_policy));

        if (diagnostics.iterations < 0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment diagnostics iterations must be non-negative")
                    .ctx("iterations", diagnostics.iterations)
            );
        }

        MATHFP_TRY(ensure_finite_nonnegative(
              diagnostics.max_load_delta
            , "max_load_delta"
        ));

        const auto used_factor = mathfp::units::as_dimless(diagnostics.used_factor);
        MATHFP_TRY(ensure_finite_nonnegative(used_factor, "used_factor"));

        if (!diagnostics.capacity_aware_enabled) {
            if (
                   diagnostics.iterations == 0
                && !diagnostics.converged
                && diagnostics.max_load_delta == 0.0
                && used_factor == 0.0
            ) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("disabled capacity-aware assignment diagnostics must be empty")
                    .ctx("iterations", diagnostics.iterations)
                    .ctx("converged", diagnostics.converged ? "true" : "false")
                    .ctx("max_load_delta", diagnostics.max_load_delta)
                    .ctx("used_factor", used_factor)
            );
        }

        if (diagnostics.iterations <= 0 || !(used_factor > 0.0)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("enabled capacity-aware assignment diagnostics require iterations and positive used factor")
                    .ctx("iterations", diagnostics.iterations)
                    .ctx("used_factor", used_factor)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<Dimless> capacity_ratio(
          double                            load
        , const VehicleJourneyItemCapacity& capacity
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(load, "load"));
        MATHFP_TRY(validate_vehicle_journey_item_capacity(capacity));
        MATHFP_TRY(ensure_positive_capacity(capacity));
        return Dimless{ load / capacity.total_capacity };
    }

    mathfp::Expected<Dimless> capacity_ratio(
          const VehicleJourneyItemLoad&     load
        , const VehicleJourneyItemCapacity& capacity
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_load(load));
        if (load.key.item != capacity.key) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity ratio load and capacity keys do not match")
                    .ctx("load_trip_id", load.key.item.trip.get())
                    .ctx("load_from_index", load.key.item.from_index.get())
                    .ctx("capacity_trip_id", capacity.key.trip.get())
                    .ctx("capacity_from_index", capacity.key.from_index.get())
            );
        }
        return capacity_ratio(load.passengers, capacity);
    }

    mathfp::Expected<Dimless> capacity_penalty(
          CapacityPenaltyPolicy policy
        , Dimless               ratio
    ) {
        MATHFP_TRY(validate_capacity_penalty_policy(policy));
        const auto raw_ratio = mathfp::units::as_dimless(ratio);
        MATHFP_TRY(ensure_finite_nonnegative(raw_ratio, "capacity_ratio"));

        switch (policy) {
            case CapacityPenaltyPolicy::VolumeCapacityRatio:
                return Dimless{ raw_ratio };

            case CapacityPenaltyPolicy::ExcessVolumeCapacityRatio:
                return Dimless{ std::max(0.0, raw_ratio - 1.0) };
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown capacity penalty policy")
                .ctx("policy", static_cast<std::int64_t>(policy))
        );
    }

    mathfp::Expected<CapacityExposure> connection_capacity_exposure(
          const Connection&                  connection
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    ) {
        MATHFP_TRY(validate_connection_trace(connection.trace));
        MATHFP_TRY(validate_vehicle_journey_item_load_state(load_state));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacity_set));
        MATHFP_TRY(validate_capacity_penalty_policy(policy));

        const auto load_lookup = build_load_lookup(load_state);
        const auto capacity_lookup = build_capacity_lookup(capacity_set);

        mathfp::CompensatedSum<double> exposure_seconds;
        for (const auto& leg : connection.trace.legs) {
            if (!is_ride_leg(leg.kind)) {
                continue;
            }

            MATHFP_TRY(accumulate_ride_leg_capacity_exposure(
                  exposure_seconds
                , leg
                , interval
                , load_lookup
                , capacity_lookup
                , policy
            ));
        }

        return make_capacity_exposure(Time{ exposure_seconds.value() });
    }

    mathfp::Expected<CapacityExposure> connection_capacity_exposure(
          const SearchConnection&            connection
        , IntervalId                         interval
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy             policy
    ) {
        return connection_capacity_exposure(
              canonical_connection(connection)
            , interval
            , load_state
            , capacity_set
            , policy
        );
    }

    mathfp::Expected<double> capacity_adjusted_perceived_journey_time(
          double           base_perceived_journey_time
        , CapacityExposure exposure
        , Dimless          factor
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(
              base_perceived_journey_time
            , "base_perceived_journey_time"
        ));
        MATHFP_TRY(validate_capacity_exposure(exposure));
        const auto raw_factor = mathfp::units::as_dimless(factor);
        MATHFP_TRY(ensure_finite_nonnegative(raw_factor, "capacity_factor"));

        const auto adjusted = base_perceived_journey_time
            + raw_factor * exposure.equivalent_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(
              adjusted
            , "capacity_adjusted_perceived_journey_time"
        ));
        return adjusted;
    }

    mathfp::Expected<double> capacity_adjusted_split_impedance(
          double           base_split_impedance
        , CapacityExposure exposure
        , Dimless          q_time
        , Dimless          perceived_journey_time_capacity_factor
    ) {
        MATHFP_TRY(ensure_finite_nonnegative(
              base_split_impedance
            , "base_split_impedance"
        ));
        MATHFP_TRY(validate_capacity_exposure(exposure));

        const auto raw_q_time = mathfp::units::as_dimless(q_time);
        const auto raw_factor = mathfp::units::as_dimless(
            perceived_journey_time_capacity_factor
        );
        MATHFP_TRY(ensure_finite_nonnegative(raw_q_time, "q_time"));
        MATHFP_TRY(ensure_finite_nonnegative(raw_factor, "capacity_factor"));

        const auto adjusted = base_split_impedance
            + raw_q_time * raw_factor * exposure.equivalent_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(
              adjusted
            , "capacity_adjusted_split_impedance"
        ));
        return adjusted;
    }

    mathfp::Expected<CapacityIterationConfig> make_capacity_iteration_config(
          std::int32_t max_iterations
        , double       absolute_load_tolerance
        , double       relative_load_tolerance
    ) {
        CapacityIterationConfig config{
              .max_iterations          = max_iterations
            , .absolute_load_tolerance = absolute_load_tolerance
            , .relative_load_tolerance = relative_load_tolerance
        };

        MATHFP_TRY(validate_capacity_iteration_config(config));
        return config;
    }

    mathfp::Expected<CapacityAwareAssignmentConfig> make_capacity_aware_assignment_config(
          bool                    capacity_aware_split_enabled
        , bool                    capacity_aware_search_enabled
        , CapacityPenaltyPolicy   penalty_policy
        , CapacityIterationConfig iteration
    ) {
        CapacityAwareAssignmentConfig config{
              .capacity_aware_split_enabled  = capacity_aware_split_enabled
            , .capacity_aware_search_enabled = capacity_aware_search_enabled
            , .penalty_policy                = penalty_policy
            , .iteration                     = iteration
        };

        MATHFP_TRY(validate_capacity_aware_assignment_config(config));
        return config;
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

    mathfp::Expected<CapacityExposure> make_capacity_exposure(
        Time equivalent_time
    ) {
        CapacityExposure exposure{
            .equivalent_time = equivalent_time
        };

        MATHFP_TRY(validate_capacity_exposure(exposure));
        return exposure;
    }

    mathfp::Expected<CapacityAwareAssignmentDiagnostics> make_capacity_aware_assignment_diagnostics(
          bool                                  capacity_aware_enabled
        , const CapacityAwareSplitDiagnostics& split_diagnostics
        , Dimless                               used_factor
        , CapacityPenaltyPolicy                 penalty_policy
    ) {
        MATHFP_TRY(validate_capacity_aware_split_diagnostics(split_diagnostics));

        if (capacity_aware_enabled != split_diagnostics.enabled) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware assignment diagnostics enabled flag disagrees with split diagnostics")
                    .ctx("capacity_aware_enabled", capacity_aware_enabled ? "true" : "false")
                    .ctx("split_enabled", split_diagnostics.enabled ? "true" : "false")
            );
        }

        CapacityAwareAssignmentDiagnostics diagnostics{
              .capacity_aware_enabled = capacity_aware_enabled
            , .iterations              = split_diagnostics.iterations
            , .converged               = split_diagnostics.converged
            , .max_load_delta          = split_diagnostics.max_load_delta
            , .used_factor             = capacity_aware_enabled ? used_factor : Dimless{ 0.0 }
            , .penalty_policy          = penalty_policy
        };

        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(diagnostics));
        return diagnostics;
    }

    CapacityAwareAssignmentDiagnostics make_capacity_aware_assignment_disabled_diagnostics(
        CapacityPenaltyPolicy penalty_policy
    ) {
        return CapacityAwareAssignmentDiagnostics{
              .capacity_aware_enabled = false
            , .iterations              = 0
            , .converged               = false
            , .max_load_delta          = 0.0
            , .used_factor             = Dimless{ 0.0 }
            , .penalty_policy          = penalty_policy
        };
    }

    CapacityAwareAssignmentConfig make_capacity_aware_assignment_disabled_config() {
        return CapacityAwareAssignmentConfig{
              .capacity_aware_split_enabled  = false
            , .capacity_aware_search_enabled = false
            , .penalty_policy                = CapacityPenaltyPolicy::VolumeCapacityRatio
            , .iteration                     = CapacityIterationConfig{}
        };
    }

    CapacityAwareSplitDiagnostics make_capacity_aware_split_disabled_diagnostics() {
        return CapacityAwareSplitDiagnostics{
              .enabled                 = false
            , .iterations              = 0
            , .converged               = false
            , .max_load_delta          = 0.0
            , .max_relative_load_delta = 0.0
        };
    }

}  // namespace timetable::domain::assignment
