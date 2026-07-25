#include "timetable/domain/assignment/search/cost/capacity_index.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment/capacity_aware/penalty.hpp"

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

}  // namespace

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

    [[nodiscard]] mathfp::Expected<std::vector<IntervalId>> search_capacity_load_intervals(
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

        MATHFP_TRY_LET(std::vector<IntervalId>, intervals, search_capacity_load_intervals(load_state));
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

        MATHFP_TRY_LET(std::vector<IntervalId>, intervals, search_capacity_load_intervals(config.load_state));
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

    [[nodiscard]] mathfp::Expected<double> search_capacity_penalty_sum(
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

}  // namespace timetable::domain::assignment
