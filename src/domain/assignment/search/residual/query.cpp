#include "timetable/domain/assignment/search/residual/query.hpp"

#include <algorithm>

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] bool has_more_budget_state(
          const DestinationResidualReachability& destination
        , const ResidualReachabilityKey&         state
        , TransferCount                          max_transfers
    ) noexcept {
        auto next_remaining = bounded_next_transfer_count(
              state.remaining_transfers
            , max_transfers
        );
        while (next_remaining.has_value()) {
            if (has_reachable_state(
                  destination
                , ResidualReachabilityKey{
                      .current_physical    = state.current_physical
                    , .phase               = state.phase
                    , .remaining_transfers = *next_remaining
                  }
            )) {
                return true;
            }
            next_remaining = bounded_next_transfer_count(*next_remaining, max_transfers);
        }
        return false;
    }

    [[nodiscard]] bool has_any_phase_state_at_endpoint(
          const DestinationResidualReachability& destination
        , EndpointKey                            endpoint
    ) noexcept {
        return std::any_of(
              destination.reachable_states.begin()
            , destination.reachable_states.end()
            , [&](const ResidualReachabilityKey& state) {
                return state.current_physical == endpoint;
            }
        );
    }

}  // namespace

    [[nodiscard]] ReachabilityDecision evaluate_residual_reachability(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept {
        const auto destination_it = reachability.destinations.find(state.destination);
        if (destination_it == reachability.destinations.end()) {
            return ReachabilityDecision{ .feasible = true };
        }

        if (is_completed(state.phase)) {
            const auto feasible = state.current_physical.kind == EndpointKind::Zone
                && state.current_physical.id == state.destination.get();
            return ReachabilityDecision{
                  .feasible = feasible
                , .rejection_reason = feasible
                    ? ReachabilityRejectionReason::UnreachableDestination
                    : ReachabilityRejectionReason::Phase
            };
        }

        const auto key = residual_reachability_key(state);
        const auto& destination_reachability = destination_it->second;
        if (has_reachable_state(destination_reachability, key)) {
            return ReachabilityDecision{ .feasible = true };
        }

        if (has_more_budget_state(destination_reachability, key, max_transfers)) {
            return ReachabilityDecision{
                  .feasible = false
                , .rejection_reason = ReachabilityRejectionReason::TransferBudget
            };
        }

        if (has_any_phase_state_at_endpoint(destination_reachability, state.current_physical)) {
            return ReachabilityDecision{
                  .feasible = false
                , .rejection_reason = ReachabilityRejectionReason::Phase
            };
        }

        return ReachabilityDecision{
              .feasible = false
            , .rejection_reason = ReachabilityRejectionReason::UnreachableDestination
        };
    }

    [[nodiscard]] bool can_have_feasible_suffix(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept {
        return evaluate_residual_reachability(
              reachability
            , state
            , max_transfers
        ).feasible;
    }

}  // namespace timetable::domain::assignment
