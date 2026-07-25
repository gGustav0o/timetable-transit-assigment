#include "timetable/domain/assignment/capacity_aware/load_state_iteration.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>

#include "timetable/domain/assignment/capacity_aware/load_state.hpp"

namespace timetable::domain::assignment {
    namespace {

        using VehicleJourneyItemLoadScalarMap = std::map<VehicleJourneyItemLoadKey, double>;

        [[nodiscard]] VehicleJourneyItemLoadScalarMap load_scalar_map(
            const VehicleJourneyItemLoadState& state
        ) {
            VehicleJourneyItemLoadScalarMap out;
            for (const auto& item : state.items) {
                out.emplace(item.key, item.passengers);
            }
            return out;
        }

        [[nodiscard]] VehicleJourneyItemLoadScalarMap load_scalar_map(
            const VehicleJourneyItemLoads& loads
        ) {
            VehicleJourneyItemLoadScalarMap out;
            for (const auto& item : loads.items) {
                out.emplace(item.key, item.passengers);
            }
            return out;
        }

        [[nodiscard]] double load_value(
              const VehicleJourneyItemLoadScalarMap& loads
            , const VehicleJourneyItemLoadKey&       key
        ) noexcept {
            const auto it = loads.find(key);
            return it == loads.end() ? 0.0 : it->second;
        }

        [[nodiscard]] std::vector<VehicleJourneyItemLoadKey> load_key_union(
              const VehicleJourneyItemLoadScalarMap& lhs
            , const VehicleJourneyItemLoadScalarMap& rhs
        ) {
            std::vector<VehicleJourneyItemLoadKey> keys;
            keys.reserve(lhs.size() + rhs.size());
            for (const auto& [key, _] : lhs) {
                keys.push_back(key);
            }
            for (const auto& [key, _] : rhs) {
                if (lhs.find(key) == lhs.end()) {
                    keys.push_back(key);
                }
            }
            std::sort(keys.begin(), keys.end());
            return keys;
        }

    }  // namespace

    CapacityLoadStateDelta load_state_delta(
          const VehicleJourneyItemLoadState& previous
        , const VehicleJourneyItemLoadState& next
    ) {
        const auto previous_map = load_scalar_map(previous);
        const auto next_map     = load_scalar_map(next);
        const auto keys         = load_key_union(previous_map, next_map);

        CapacityLoadStateDelta delta{};
        for (const auto& key : keys) {
            const auto previous_value = load_value(previous_map, key);
            const auto next_value     = load_value(next_map, key);
            const auto absolute_delta = std::abs(next_value - previous_value);
            const auto scale = std::max(
                  { 1.0, std::abs(previous_value), std::abs(next_value) }
            );

            delta.max_absolute = std::max(delta.max_absolute, absolute_delta);
            delta.max_relative = std::max(delta.max_relative, absolute_delta / scale);
        }

        return delta;
    }

    mathfp::Expected<VehicleJourneyItemLoadState> msa_update_load_state(
          const VehicleJourneyItemLoadState& previous
        , const VehicleJourneyItemLoads&     candidate
        , double                             alpha
    ) {
        if (!(alpha > 0.0 && alpha <= 1.0) || !std::isfinite(alpha)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("capacity-aware MSA alpha must be finite and in (0, 1]")
                    .ctx("alpha", alpha)
            );
        }

        const auto previous_map  = load_scalar_map(previous);
        const auto candidate_map = load_scalar_map(candidate);
        const auto keys          = load_key_union(previous_map, candidate_map);

        std::vector<VehicleJourneyItemLoad> updated;
        updated.reserve(keys.size());

        for (const auto& key : keys) {
            const auto previous_value  = load_value(previous_map, key);
            const auto candidate_value = load_value(candidate_map, key);
            const auto next_value =
                (1.0 - alpha) * previous_value + alpha * candidate_value;

            if (!std::isfinite(next_value) || next_value < 0.0) {
                return mathfp::unexpected(
                    mathfp::domain_error("capacity-aware MSA produced invalid vehicle journey item load")
                        .ctx("interval_id", key.interval.get())
                        .ctx("trip_id", key.item.trip.get())
                        .ctx("from_index", key.item.from_index.get())
                        .ctx("load", next_value)
                );
            }

            if (next_value > 0.0) {
                updated.push_back(
                    VehicleJourneyItemLoad{
                          .key        = key
                        , .passengers = next_value
                    }
                );
            }
        }

        return make_vehicle_journey_item_load_state(std::move(updated));
    }

    bool capacity_iteration_converged(
          const CapacityIterationConfig& iteration
        , CapacityLoadStateDelta         delta
    ) noexcept {
        return delta.max_absolute <= iteration.absolute_load_tolerance
            || delta.max_relative <= iteration.relative_load_tolerance;
    }

}  // namespace timetable::domain::assignment
