#include "timetable/domain/assignment/search/residual/types.hpp"

#include <functional>

namespace timetable::domain::assignment {

    bool operator==(
          const ResidualReachabilityKey& lhs
        , const ResidualReachabilityKey& rhs
    ) noexcept {
        return lhs.current_physical    == rhs.current_physical
            && lhs.phase               == rhs.phase
            && lhs.remaining_transfers == rhs.remaining_transfers;
    }

    std::size_t ResidualReachabilityKeyHash::operator()(
        const ResidualReachabilityKey& key
    ) const noexcept {
        std::size_t seed = 17u;
        seed = seed * 31u + std::hash<EndpointKey>{}(key.current_physical);
        seed = seed * 31u + std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.phase));
        seed = seed * 31u + std::hash<std::int32_t>{}(key.remaining_transfers.get());
        return seed;
    }

    [[nodiscard]] ResidualReachabilityKey residual_reachability_key(
        const RelaxedSuffixState& state
    ) noexcept {
        return ResidualReachabilityKey{
              .current_physical    = state.current_physical
            , .phase               = state.phase
            , .remaining_transfers = state.remaining_transfers
        };
    }

    [[nodiscard]] bool has_reachable_state(
          const DestinationResidualReachability& destination
        , const ResidualReachabilityKey&         state
    ) noexcept {
        return destination.reachable_states.contains(state);
    }

}  // namespace timetable::domain::assignment
