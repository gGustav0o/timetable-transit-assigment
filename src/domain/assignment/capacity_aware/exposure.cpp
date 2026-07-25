#include "timetable/domain/assignment/capacity_aware/exposure.hpp"

#include <cmath>
#include <map>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/capacity/load_projection.hpp"
#include "timetable/domain/assignment/capacity_aware/load_state.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool finite_nonnegative(
            double value
        ) noexcept {
            return std::isfinite(value) && value >= 0.0;
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

        [[nodiscard]] mathfp::Expected<double> ride_support_leg_duration_seconds(
            const DayPathRideSupportLeg& leg
        ) {
            const auto duration = leg.arrival.value() - leg.departure.value();
            if (finite_nonnegative(duration)) {
                return duration;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity exposure cannot be computed from a negative or non-finite day-path support duration")
                    .ctx("departure", leg.departure.value())
                    .ctx("arrival", leg.arrival.value())
                    .ctx("duration", duration)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> accumulate_ride_support_capacity_exposure(
              mathfp::CompensatedSum<double>& exposure_seconds
            , const DayPathRideSupportLeg&    leg
            , IntervalId                       interval
            , const LoadLookup&                load_lookup
            , const CapacityLookup&            capacity_lookup
            , CapacityPenaltyPolicy            policy
        ) {
            const auto item_count = leg.to_index.get() - leg.from_index.get();
            if (!(item_count > 0)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("day-path support ride leg must occupy at least one vehicle journey item")
                        .ctx("trip_id", leg.trip.get())
                        .ctx("from_index", leg.from_index.get())
                        .ctx("to_index", leg.to_index.get())
                );
            }
            MATHFP_TRY_LET(
                  double
                , leg_duration
                , ride_support_leg_duration_seconds(leg)
            );
            const auto item_duration = leg_duration / static_cast<double>(item_count);

            for (auto index = leg.from_index.get(); index < leg.to_index.get(); ++index) {
                const auto item = VehicleJourneyItemKey{
                      .trip       = leg.trip
                    , .from_index = RoutePosition{ index }
                };
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

    mathfp::Expected<CapacityExposure> connection_capacity_exposure(
          const Connection&                    connection
        , IntervalId                           interval
        , const VehicleJourneyItemLoadState&   load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy                policy
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

    mathfp::Expected<CapacityExposure> day_path_support_capacity_exposure(
          const DayPathSupportDescriptor&      support
        , IntervalId                           interval
        , const VehicleJourneyItemLoadState&   load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
        , CapacityPenaltyPolicy                policy
    ) {
        MATHFP_TRY(validate_vehicle_journey_item_load_state(load_state));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacity_set));
        MATHFP_TRY(validate_capacity_penalty_policy(policy));

        const auto load_lookup = build_load_lookup(load_state);
        const auto capacity_lookup = build_capacity_lookup(capacity_set);

        mathfp::CompensatedSum<double> exposure_seconds;
        for (const auto& leg : support.ride_legs) {
            MATHFP_TRY(accumulate_ride_support_capacity_exposure(
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

}  // namespace timetable::domain::assignment
