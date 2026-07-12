#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/assignment/capacity_aware_assignment.hpp"
#include "timetable/domain/impedance.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Search-cost evaluation mode for branch-and-bound and choice.
     *
     * BaseOnly is the current timetable search cost. CapacityAware means that
     * vehicle journey item loads are fixed exogenously for the whole search
     * evaluation and add a non-negative capacity exposure term.
     */
    enum class SearchCostMode : std::uint8_t {
          BaseOnly
        , CapacityAware
    };

    inline constexpr std::array kSearchCostModeTokens{
          timetable::EnumStringEntry<SearchCostMode>{
              SearchCostMode::BaseOnly, "base_only"
          }
        , timetable::EnumStringEntry<SearchCostMode>{
              SearchCostMode::CapacityAware, "capacity_aware"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SearchCostMode value
    ) noexcept {
        return timetable::enum_to_string(value, kSearchCostModeTokens);
    }

    /**
     * @brief Immutable lookup indexes for capacity-aware search evaluation.
     *
     * The maps store positions, not pointers, so the index remains valid after
     * SearchCapacityCostConfig moves. It is derived from load_state and
     * capacity_set and must be rebuilt when either collection changes.
     */
    struct SearchCapacityTripPenaltyKey final {
        IntervalId interval;
        TripId     trip;

        auto operator<=>(const SearchCapacityTripPenaltyKey&) const = default;
    };

    struct SearchCapacityTripPenaltyPrefix final {
        std::vector<RoutePosition> positions{};
        std::vector<double>        cumulative_penalties{};
    };

    struct SearchCapacityCostIndex final {
        std::map<VehicleJourneyItemLoadKey, std::size_t> load_positions{};
        std::map<VehicleJourneyItemKey, std::size_t>     capacity_positions{};
        std::map<TripId, std::vector<RoutePosition>>     capacity_trip_positions{};
        std::map<SearchCapacityTripPenaltyKey, SearchCapacityTripPenaltyPrefix> penalty_prefixes{};
    };

    /**
     * @brief Fixed capacity-cost data for one capacity-aware search iteration.
     *
     * load_state is an exogenous snapshot. It must not be mutated while a
     * branch-and-bound run evaluates dominance, pruning and choice metrics.
     * index is built once from this fixed snapshot and capacity_set by the
     * SearchCostContext factory; hot search-cost evaluation must use this
     * immutable lookup data instead of scanning capacity/load vectors.
     */
    struct SearchCapacityCostConfig final {
        Dimless                       volume_capacity_ratio{};
        CapacityPenaltyPolicy         penalty_policy{ CapacityPenaltyPolicy::VolumeCapacityRatio };
        VehicleJourneyItemLoadState   load_state{};
        VehicleJourneyItemCapacitySet capacity_set{};
        SearchCapacityCostIndex       index{};
    };

    /**
     * @brief Full search-cost context shared by search and choice.
     *
     * The same context must be used for partial pruning, complete-connection
     * dominance and final choice filtering. Otherwise search and choice would
     * optimize different generalized costs.
     */
    struct SearchCostContext final {
        SearchCostMode          mode{ SearchCostMode::BaseOnly };
        SearchImpedance         impedance{};
        double                  fare_scale{};
        SearchCapacityCostConfig capacity{};
    };

    /**
     * @brief Search-cost components with the capacity term kept explicit.
     */
    struct SearchCostComponents final {
        ConnectionImpedanceComponents base{};
        CapacityExposure              capacity_exposure{};
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_cost_mode(
        SearchCostMode mode
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_impedance_weights(
        const SearchImpedance& impedance
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_capacity_cost_config(
          const SearchCapacityCostConfig& config
        , SearchCostMode                  mode
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_cost_context(
        const SearchCostContext& context
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_cost_components(
        const SearchCostComponents& components
    );

    [[nodiscard]] mathfp::Expected<SearchCapacityCostConfig> make_base_search_capacity_cost_config();

    [[nodiscard]] mathfp::Expected<SearchCostContext> make_base_search_cost_context(
          SearchImpedance impedance
        , double          fare_scale
    );

    [[nodiscard]] mathfp::Expected<SearchCostContext> make_capacity_aware_search_cost_context(
          SearchImpedance               impedance
        , double                        fare_scale
        , CapacityPenaltyPolicy         penalty_policy
        , VehicleJourneyItemLoadState   load_state
        , VehicleJourneyItemCapacitySet capacity_set
    );

    [[nodiscard]] mathfp::Expected<SearchCostComponents> make_search_cost_components(
          ConnectionImpedanceComponents base
        , CapacityExposure              capacity_exposure
    );

    [[nodiscard]] mathfp::Expected<double> base_search_impedance(
          const ConnectionImpedanceComponents& components
        , const SearchImpedance&               impedance
        , double                               fare_scale
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const Connection&                  connection
        , IntervalId                         interval
        , const SearchCapacityCostConfig&    capacity
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const ConnectionLeg&               ride_leg
        , IntervalId                         interval
        , const SearchCapacityCostConfig&    capacity
    );

    [[nodiscard]] mathfp::Expected<CapacityExposure> search_capacity_exposure(
          const SearchConnection&            connection
        , IntervalId                         interval
        , const SearchCapacityCostConfig&    capacity
    );

    [[nodiscard]] mathfp::Expected<double> capacity_adjusted_search_impedance(
          double           base_impedance
        , CapacityExposure exposure
        , Dimless          volume_capacity_ratio
    );

    /**
     * @brief Evaluate search impedance for already validated cost data.
     *
     * Full SearchCostContext validation belongs to make_*_search_cost_context
     * and pipeline boundaries. This function is intentionally lightweight
     * because it is called from branch-and-bound dominance and retention loops.
     */
    [[nodiscard]] mathfp::Expected<double> search_impedance(
          const SearchCostComponents& components
        , const SearchCostContext&    context
    );

}  // namespace timetable::domain::assignment
