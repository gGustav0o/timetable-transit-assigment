#include "timetable/domain/assignment/search/residual/validation.hpp"

#include <cmath>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_destination_residual_reachability(
          ZoneId                                destination
        , const DestinationResidualReachability& reachability
        , TransferCount                         max_transfers
    ) {
        const auto destination_endpoint = endpoint_key(destination);

        auto remaining = TransferCount{0};
        while (remaining <= max_transfers) {
            if (!has_reachable_state(
                  reachability
                , ResidualReachabilityKey{
                      .current_physical    = destination_endpoint
                    , .phase               = SearchBranchPhase::Completed
                    , .remaining_transfers = remaining
                  }
            )) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability misses destination completion seed")
                        .ctx("destination", destination.get())
                        .ctx("remaining_transfers", remaining.get())
                );
            }

            const auto next = bounded_next_transfer_count(remaining, max_transfers);
            if (!next.has_value()) {
                break;
            }
            remaining = *next;
        }

        for (const auto& state : reachability.reachable_states) {
            if (state.remaining_transfers.get() < 0
                || state.remaining_transfers.get() > max_transfers.get()) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains state outside transfer budget")
                        .ctx("destination", destination.get())
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                        .ctx("max_transfers", max_transfers.get())
                );
            }

            if (state.phase == SearchBranchPhase::Completed
                && state.current_physical != destination_endpoint) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains non-destination completed state")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                );
            }

            if (state.phase == SearchBranchPhase::AtOrigin
                && state.current_physical.kind != EndpointKind::Zone) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains at-origin state outside zone")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }

            if ((state.phase == SearchBranchPhase::BeforeFirstBoarding
                    || state.phase == SearchBranchPhase::AfterTimedRide
                    || state.phase == SearchBranchPhase::AfterTransferWalk)
                && state.current_physical.kind != EndpointKind::Stop) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains stop-phase state outside stop")
                        .ctx("destination", destination.get())
                        .ctx("phase", static_cast<std::int64_t>(state.phase))
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }

            if (state.phase == SearchBranchPhase::Completed
                && state.current_physical.kind != EndpointKind::Zone) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains completed state outside zone")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }

            if (const auto next_remaining = bounded_next_transfer_count(
                  state.remaining_transfers
                , max_transfers
            )) {
                const auto relaxed_more_budget_state = ResidualReachabilityKey{
                      .current_physical    = state.current_physical
                    , .phase               = state.phase
                    , .remaining_transfers = *next_remaining
                };
                if (!has_reachable_state(reachability, relaxed_more_budget_state)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability violates transfer-budget monotonicity")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }
            }

            const auto lower_bounds = reachability.suffix_lower_bounds.find(state);
            if (lower_bounds == reachability.suffix_lower_bounds.end()) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability state misses suffix lower bounds")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }
            if (
                   !std::isfinite(lower_bounds->second.journey_time.value())
                || !std::isfinite(lower_bounds->second.impedance)
                || lower_bounds->second.journey_time.value() < 0.0
                || lower_bounds->second.impedance < 0.0
                || lower_bounds->second.transfers.get() < 0
            ) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual suffix lower bounds contain invalid value")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                );
            }
        }

        for (const auto& [state, lower_bounds] : reachability.suffix_lower_bounds) {
            (void)lower_bounds;
            if (!has_reachable_state(reachability, state)) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual suffix lower bounds contain unreachable state")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_residual_reachability(
          const ResidualReachability& reachability
        , TransferCount               max_transfers
    ) {
        for (const auto& [destination, destination_reachability] : reachability.destinations) {
            MATHFP_TRY(validate_destination_residual_reachability(
                  destination
                , destination_reachability
                , max_transfers
            ));
        }
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
