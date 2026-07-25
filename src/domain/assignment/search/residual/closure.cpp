#include "timetable/domain/assignment/search/residual/closure.hpp"

#include <deque>

#include "timetable/domain/assignment/search/residual/transition.hpp"

namespace timetable::domain::assignment {
namespace {

    template <typename Fn>
    void for_each_transfer_count_up_to(TransferCount max_transfers, Fn&& fn) {
        auto remaining = TransferCount{0};
        while (remaining <= max_transfers) {
            fn(remaining);

            const auto next = bounded_next_transfer_count(remaining, max_transfers);
            if (!next.has_value()) {
                break;
            }
            remaining = *next;
        }
    }

    void enqueue_reachable_state(
          ResidualReachabilityStateSet&       reachable_states
        , std::deque<ResidualReachabilityKey>& frontier
        , ResidualReachabilityKey              state
    ) {
        if (reachable_states.insert(state).second) {
            frontier.push_back(state);
        }
    }

    void enqueue_residual_predecessors(
          const ResidualReverseGraph&          graph
        , ResidualReachabilityStateSet&         reachable_states
        , std::deque<ResidualReachabilityKey>& frontier
        , const ResidualReachabilityKey&        state
        , TransferCount                         max_transfers
    ) {
        for (const auto& transition : residual_predecessor_transitions(
              graph
            , state
            , max_transfers
        )) {
            enqueue_reachable_state(
                  reachable_states
                , frontier
                , transition.predecessor
            );
        }
    }

}  // namespace

    [[nodiscard]] ResidualReachabilityStateSet build_residual_reachable_states(
          const ResidualReverseGraph& graph
        , ZoneId                      destination
        , TransferCount               max_transfers
    ) {
        ResidualReachabilityStateSet reachable_states;
        std::deque<ResidualReachabilityKey> frontier;
        const auto destination_endpoint = endpoint_key(destination);

        for_each_transfer_count_up_to(max_transfers, [&](const TransferCount remaining) {
            enqueue_reachable_state(
                  reachable_states
                , frontier
                , ResidualReachabilityKey{
                      .current_physical    = destination_endpoint
                    , .phase               = SearchBranchPhase::Completed
                    , .remaining_transfers = remaining
                  }
            );
        });

        while (!frontier.empty()) {
            const auto state = frontier.front();
            frontier.pop_front();

            enqueue_residual_predecessors(
                  graph
                , reachable_states
                , frontier
                , state
                , max_transfers
            );
        }

        return reachable_states;
    }

}  // namespace timetable::domain::assignment
