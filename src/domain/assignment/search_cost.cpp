#include "timetable/domain/assignment/search_cost.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
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
                && config.capacity_set.items.empty()
                && config.index.load_positions.empty()
                && config.index.capacity_positions.empty()
                && config.index.capacity_trip_positions.empty()
                && config.index.penalty_prefixes.empty();
        }

        [[nodiscard]] mathfp::Expected<std::vector<IntervalId>> load_intervals(
            const VehicleJourneyItemLoadState& load_state
        ) {
            std::vector<IntervalId> intervals;
            intervals.reserve(load_state.items.size());
            for (const auto& load : load_state.items) {
                intervals.push_back(load.key.interval);
            }
            std::sort(intervals.begin(), intervals.end());
            intervals.erase(
                  std::unique(intervals.begin(), intervals.end())
                , intervals.end()
            );
            return intervals;
        }

        [[nodiscard]] mathfp::Expected<SearchCapacityCostIndex> build_search_capacity_cost_index(
              const VehicleJourneyItemLoadState&   load_state
            , const VehicleJourneyItemCapacitySet& capacity_set
            , CapacityPenaltyPolicy                penalty_policy
        ) {
            SearchCapacityCostIndex index;
            for (std::size_t i = 0; i < load_state.items.size(); ++i) {
                index.load_positions.emplace(load_state.items[i].key, i);
            }
            for (std::size_t i = 0; i < capacity_set.items.size(); ++i) {
                const auto& capacity = capacity_set.items[i];
                index.capacity_positions.emplace(capacity.key, i);
                index.capacity_trip_positions[capacity.key.trip].push_back(
                    capacity.key.from_index
                );
            }

            for (auto& [trip, positions] : index.capacity_trip_positions) {
                (void)trip;
                std::sort(positions.begin(), positions.end());
                positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
            }

            MATHFP_TRY_LET(std::vector<IntervalId>, intervals, load_intervals(load_state));
            for (const auto interval : intervals) {
                for (const auto& [trip, positions] : index.capacity_trip_positions) {
                    SearchCapacityTripPenaltyPrefix prefix;
                    prefix.positions = positions;
                    prefix.cumulative_penalties.reserve(positions.size() + 1);
                    prefix.cumulative_penalties.push_back(0.0);

                    for (const auto position : positions) {
                        const auto item = VehicleJourneyItemKey{
                              .trip       = trip
                            , .from_index = position
                        };
                        const auto cap_it = index.capacity_positions.find(item);
                        if (cap_it == index.capacity_positions.end()
                            || cap_it->second >= capacity_set.items.size()) {
                            return mathfp::unexpected(
                                mathfp::invalid_arg("capacity-aware search capacity prefix references missing capacity")
                                    .ctx("trip_id", trip.get())
                                    .ctx("from_index", position.get())
                            );
                        }

                        double passengers = 0.0;
                        const auto load_it = index.load_positions.find(
                            VehicleJourneyItemLoadKey{
                                  .interval = interval
                                , .item     = item
                            }
                        );
                        if (load_it != index.load_positions.end()
                            && load_it->second < load_state.items.size()) {
                            passengers = load_state.items[load_it->second].passengers;
                        }

                        MATHFP_TRY_LET(
                              Dimless
                            , ratio
                            , capacity_ratio(passengers, capacity_set.items[cap_it->second])
                        );
                        MATHFP_TRY_LET(
                              Dimless
                            , penalty
                            , capacity_penalty(penalty_policy, ratio)
                        );
                        prefix.cumulative_penalties.push_back(
                              prefix.cumulative_penalties.back()
                            + mathfp::units::as_dimless(penalty)
                        );
                    }

                    index.penalty_prefixes.emplace(
                          SearchCapacityTripPenaltyKey{ .interval = interval, .trip = trip }
                        , std::move(prefix)
                    );
                }
            }

            return index;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_capacity_cost_index(
            const SearchCapacityCostConfig& config
        ) {
            if (config.index.load_positions.size() != config.load_state.items.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware search load index size mismatch")
                        .ctx("load_items", static_cast<std::int64_t>(config.load_state.items.size()))
                        .ctx("index_items", static_cast<std::int64_t>(config.index.load_positions.size()))
                );
            }
            if (config.index.capacity_positions.size() != config.capacity_set.items.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware search capacity index size mismatch")
                        .ctx("capacity_items", static_cast<std::int64_t>(config.capacity_set.items.size()))
                        .ctx("index_items", static_cast<std::int64_t>(config.index.capacity_positions.size()))
                );
            }

            for (std::size_t i = 0; i < config.load_state.items.size(); ++i) {
                const auto& load = config.load_state.items[i];
                const auto it = config.index.load_positions.find(load.key);
                if (it == config.index.load_positions.end() || it->second != i) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search load index is inconsistent")
                            .ctx("interval_id", load.key.interval.get())
                            .ctx("trip_id", load.key.item.trip.get())
                            .ctx("from_index", load.key.item.from_index.get())
                            .ctx("expected_position", static_cast<std::int64_t>(i))
                            .ctx(
                                  "actual_position"
                                , it == config.index.load_positions.end()
                                    ? static_cast<std::int64_t>(-1)
                                    : static_cast<std::int64_t>(it->second)
                              )
                    );
                }
            }

            for (std::size_t i = 0; i < config.capacity_set.items.size(); ++i) {
                const auto& capacity = config.capacity_set.items[i];
                const auto it = config.index.capacity_positions.find(capacity.key);
                if (it == config.index.capacity_positions.end() || it->second != i) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search capacity index is inconsistent")
                            .ctx("trip_id", capacity.key.trip.get())
                            .ctx("from_index", capacity.key.from_index.get())
                            .ctx("expected_position", static_cast<std::int64_t>(i))
                            .ctx(
                                  "actual_position"
                                , it == config.index.capacity_positions.end()
                                    ? static_cast<std::int64_t>(-1)
                                    : static_cast<std::int64_t>(it->second)
                              )
                    );
                }

                const auto trip_it = config.index.capacity_trip_positions.find(capacity.key.trip);
                if (trip_it == config.index.capacity_trip_positions.end()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search trip capacity index is missing trip")
                            .ctx("trip_id", capacity.key.trip.get())
                    );
                }
                if (!std::binary_search(
                      trip_it->second.begin()
                    , trip_it->second.end()
                    , capacity.key.from_index
                )) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search trip capacity index is missing position")
                            .ctx("trip_id", capacity.key.trip.get())
                            .ctx("from_index", capacity.key.from_index.get())
                    );
                }
            }

            MATHFP_TRY_LET(std::vector<IntervalId>, intervals, load_intervals(config.load_state));
            const auto expected_prefix_count =
                intervals.size() * config.index.capacity_trip_positions.size();
            if (config.index.penalty_prefixes.size() != expected_prefix_count) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware search penalty prefix index size mismatch")
                        .ctx("intervals", static_cast<std::int64_t>(intervals.size()))
                        .ctx("trips", static_cast<std::int64_t>(config.index.capacity_trip_positions.size()))
                        .ctx("prefixes", static_cast<std::int64_t>(config.index.penalty_prefixes.size()))
                );
            }

            for (const auto& [key, prefix] : config.index.penalty_prefixes) {
                const auto trip_it = config.index.capacity_trip_positions.find(key.trip);
                if (trip_it == config.index.capacity_trip_positions.end()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search penalty prefix references missing trip")
                            .ctx("interval_id", key.interval.get())
                            .ctx("trip_id", key.trip.get())
                    );
                }
                if (prefix.positions != trip_it->second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search penalty prefix positions mismatch")
                            .ctx("interval_id", key.interval.get())
                            .ctx("trip_id", key.trip.get())
                    );
                }
                if (prefix.cumulative_penalties.size() != prefix.positions.size() + 1) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware search penalty prefix cumulative size mismatch")
                            .ctx("interval_id", key.interval.get())
                            .ctx("trip_id", key.trip.get())
                            .ctx("positions", static_cast<std::int64_t>(prefix.positions.size()))
                            .ctx("prefix_values", static_cast<std::int64_t>(prefix.cumulative_penalties.size()))
                    );
                }
                for (std::size_t i = 1; i < prefix.cumulative_penalties.size(); ++i) {
                    if (!finite_nonnegative(prefix.cumulative_penalties[i])
                        || prefix.cumulative_penalties[i] < prefix.cumulative_penalties[i - 1]) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("capacity-aware search penalty prefix must be finite and non-decreasing")
                                .ctx("interval_id", key.interval.get())
                                .ctx("trip_id", key.trip.get())
                                .ctx("prefix_index", static_cast<std::int64_t>(i))
                                .ctx("value", prefix.cumulative_penalties[i])
                        );
                    }
                }
            }

            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<double> capacity_penalty_sum(
              const SearchCapacityCostConfig& config
            , IntervalId                      interval
            , TripId                          trip
            , RoutePosition                   first
            , RoutePosition                   last
        ) {
            if (!(first < last)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware search penalty range must satisfy first < last")
                        .ctx("trip_id", trip.get())
                        .ctx("first", first.get())
                        .ctx("last", last.get())
                );
            }

            const auto trip_it = config.index.capacity_trip_positions.find(trip);
            if (trip_it == config.index.capacity_trip_positions.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware search cost is missing trip capacity")
                        .ctx("trip_id", trip.get())
                );
            }

            const auto& positions = trip_it->second;
            const auto begin = std::lower_bound(positions.begin(), positions.end(), first);
            const auto end   = std::lower_bound(positions.begin(), positions.end(), last);
            const auto observed_count = static_cast<std::int64_t>(end - begin);
            const auto expected_count = static_cast<std::int64_t>(last.get() - first.get());
            if (observed_count != expected_count) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware search cost is missing vehicle journey item capacity in occupied range")
                        .ctx("trip_id", trip.get())
                        .ctx("from_index", first.get())
                        .ctx("to_index", last.get())
                        .ctx("observed_items", observed_count)
                        .ctx("expected_items", expected_count)
                );
            }

            const auto prefix_it = config.index.penalty_prefixes.find(
                SearchCapacityTripPenaltyKey{ .interval = interval, .trip = trip }
            );
            if (prefix_it == config.index.penalty_prefixes.end()) {
                return 0.0;
            }

            const auto& prefix = prefix_it->second;
            const auto begin_index = static_cast<std::size_t>(begin - positions.begin());
            const auto end_index   = static_cast<std::size_t>(end - positions.begin());
            if (end_index >= prefix.cumulative_penalties.size()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("capacity-aware search penalty prefix range is inconsistent")
                        .ctx("trip_id", trip.get())
                        .ctx("from_index", first.get())
                        .ctx("to_index", last.get())
                );
            }

            const auto sum =
                  prefix.cumulative_penalties[end_index]
                - prefix.cumulative_penalties[begin_index];
            MATHFP_TRY(ensure_finite_nonnegative(sum, "capacity_penalty_sum"));
            return sum;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> ensure_search_ride_leg_occupancy_identity(
            const ConnectionLeg& ride_leg
        ) {
            if (
                   ride_leg.trip.has_value()
                && ride_leg.occurrence_from.has_value()
                && ride_leg.occurrence_to.has_value()
            ) {
                return mathfp::kUnit;
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search ride leg is missing occupancy identity")
                    .ctx("has_trip"           , ride_leg.trip.has_value() ? "true" : "false")
                    .ctx("has_occurrence_from", ride_leg.occurrence_from.has_value() ? "true" : "false")
                    .ctx("has_occurrence_to"  , ride_leg.occurrence_to.has_value() ? "true" : "false")
            );
        }

        [[nodiscard]] double base_search_impedance_unchecked(
              const ConnectionImpedanceComponents& components
            , const SearchImpedance&               impedance
            , double                               fare_scale
        ) noexcept {
            return connection_impedance_value(
                  components
                , impedance
                , fare_scale
            );
        }

        [[nodiscard]] double capacity_adjusted_search_impedance_unchecked(
              double           base_impedance
            , CapacityExposure exposure
            , Dimless          volume_capacity_ratio
        ) noexcept {
            return base_impedance
                + mathfp::units::as_dimless(volume_capacity_ratio)
                    * exposure.equivalent_time.value();
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
                MATHFP_TRY(validate_search_capacity_cost_index(config));
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

    mathfp::Expected<double> base_search_impedance(
          const ConnectionImpedanceComponents& components
        , const SearchImpedance&               impedance
        , double                               fare_scale
    ) {
        MATHFP_TRY(validate_connection_impedance_components(components));
        MATHFP_TRY(validate_search_impedance_weights(impedance));
        MATHFP_TRY(ensure_finite_nonnegative(fare_scale, "fare_scale"));

        const auto value = base_search_impedance_unchecked(
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
        mathfp::CompensatedSum<double> exposure_seconds;
        for (const auto& leg : connection.trace.legs) {
            if (!is_ride_leg(leg.kind)) {
                continue;
            }

            MATHFP_TRY_LET(
                  CapacityExposure
                , exposure
                , search_capacity_exposure(leg, interval, capacity)
            );
            exposure_seconds.add(exposure.equivalent_time.value());
        }

        return make_capacity_exposure(Time{ exposure_seconds.value() });
    }

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const ConnectionLeg&            ride_leg
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        if (!is_ride_leg(ride_leg.kind)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search exposure can only be computed for ride legs")
                    .ctx("leg_kind", std::string(to_string(ride_leg.kind)))
            );
        }
        MATHFP_TRY(ensure_search_ride_leg_occupancy_identity(ride_leg));

        const auto duration = ride_leg.end_time.value() - ride_leg.start_time.value();
        MATHFP_TRY(ensure_finite_nonnegative(duration, "ride_leg_duration"));
        const auto first = ride_leg.occurrence_from->position.get();
        const auto last  = ride_leg.occurrence_to->position.get();
        if (!(first < last)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware search ride leg occupancy interval must satisfy from_index < to_index")
                    .ctx("trip_id"   , ride_leg.trip->get())
                    .ctx("from_index", first)
                    .ctx("to_index"  , last)
            );
        }
        const auto item_duration = duration
            / static_cast<double>(last - first);

        MATHFP_TRY_LET(
              double
            , penalty_sum
            , capacity_penalty_sum(
                  capacity
                , interval
                , *ride_leg.trip
                , RoutePosition{ first }
                , RoutePosition{ last }
            )
        );
        return make_capacity_exposure(Time{ item_duration * penalty_sum });
    }

    mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const SearchConnection&         connection
        , IntervalId                      interval
        , const SearchCapacityCostConfig& capacity
    ) {
        return search_capacity_exposure(
              canonical_connection(connection)
            , interval
            , capacity
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

        const auto adjusted = capacity_adjusted_search_impedance_unchecked(
              base_impedance
            , exposure
            , volume_capacity_ratio
        );
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
        const auto base = base_search_impedance_unchecked(
              components.base
            , context.impedance
            , context.fare_scale
        );

        switch (context.mode) {
            case SearchCostMode::BaseOnly:
                MATHFP_TRY(ensure_finite_nonnegative(base, "base_search_impedance"));
                return base;

            case SearchCostMode::CapacityAware: {
                const auto adjusted = capacity_adjusted_search_impedance_unchecked(
                      base
                    , components.capacity_exposure
                    , context.capacity.volume_capacity_ratio
                );
                MATHFP_TRY(ensure_finite_nonnegative(
                      adjusted
                    , "capacity_adjusted_search_impedance"
                ));
                return adjusted;
            }
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(context.mode))
        );
    }

}  // namespace timetable::domain::assignment
