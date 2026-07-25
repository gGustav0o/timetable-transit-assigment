#include "timetable/domain/assignment/search/residual/transition.hpp"

#include "timetable/domain/impedance.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] std::vector<ResidualPredecessorTransition> residual_predecessor_transitions(
          const ResidualReverseGraph&   graph
        , const ResidualReachabilityKey& state
        , TransferCount                  max_transfers
    ) {
        auto transitions = std::vector<ResidualPredecessorTransition>{};

        const auto walk_predecessors = graph.walk_predecessors_by_node.find(state.current_physical);
        if (walk_predecessors != graph.walk_predecessors_by_node.end()) {
            if (state.phase == SearchBranchPhase::Completed) {
                for (const auto edge : walk_predecessors->second) {
                    if (edge.predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    transitions.push_back(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTimedRide
                              , .remaining_transfers = state.remaining_transfers
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::EgressWalk
                    });
                }
            } else if (state.phase == SearchBranchPhase::AfterTransferWalk) {
                for (const auto edge : walk_predecessors->second) {
                    if (edge.predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    transitions.push_back(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTimedRide
                              , .remaining_transfers = state.remaining_transfers
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::TransferWalk
                    });
                }
            } else if (state.phase == SearchBranchPhase::BeforeFirstBoarding) {
                for (const auto edge : walk_predecessors->second) {
                    if (edge.predecessor.kind != EndpointKind::Zone) {
                        continue;
                    }
                    transitions.push_back(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AtOrigin
                              , .remaining_transfers = state.remaining_transfers
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::AccessWalk
                    });
                }
            }
        }

        if (state.phase != SearchBranchPhase::AfterTimedRide) {
            return transitions;
        }

        const auto timed_predecessors = graph.timed_predecessors_by_node.find(state.current_physical);
        if (timed_predecessors == graph.timed_predecessors_by_node.end()) {
            return transitions;
        }

        for (const auto edge : timed_predecessors->second) {
            if (edge.predecessor.kind != EndpointKind::Stop) {
                continue;
            }

            transitions.push_back(ResidualPredecessorTransition{
                  .predecessor = ResidualReachabilityKey{
                        .current_physical    = edge.predecessor
                      , .phase               = SearchBranchPhase::BeforeFirstBoarding
                      , .remaining_transfers = state.remaining_transfers
                    }
                , .run_time    = edge.run_time
                , .kind        = ResidualTransitionKind::FirstTimedRide
            });

            if (const auto next_remaining = bounded_next_transfer_count(
                  state.remaining_transfers
                , max_transfers
            )) {
                transitions.push_back(ResidualPredecessorTransition{
                      .predecessor = ResidualReachabilityKey{
                            .current_physical    = edge.predecessor
                          , .phase               = SearchBranchPhase::AfterTimedRide
                          , .remaining_transfers = *next_remaining
                        }
                    , .run_time    = edge.run_time
                    , .kind        = ResidualTransitionKind::TransferTimedRide
                });
                transitions.push_back(ResidualPredecessorTransition{
                      .predecessor = ResidualReachabilityKey{
                            .current_physical    = edge.predecessor
                          , .phase               = SearchBranchPhase::AfterTransferWalk
                          , .remaining_transfers = *next_remaining
                        }
                    , .run_time    = edge.run_time
                    , .kind        = ResidualTransitionKind::TransferTimedRide
                });
            }
        }

        return transitions;
    }

    [[nodiscard]] double residual_transition_journey_time(
        const ResidualPredecessorTransition& transition
    ) noexcept {
        return transition.run_time.value();
    }

    [[nodiscard]] double residual_transition_transfer_count(
        const ResidualPredecessorTransition& transition
    ) noexcept {
        return transition.kind == ResidualTransitionKind::TransferTimedRide ? 1.0 : 0.0;
    }

    [[nodiscard]] double residual_transition_impedance(
          const ResidualPredecessorTransition& transition
        , const SearchImpedance&               impedance
        , double                               fare_scale
    ) noexcept {
        ConnectionImpedanceComponents components{};
        switch (transition.kind) {
            case ResidualTransitionKind::AccessWalk:
                components.access_time = transition.run_time;
                break;
            case ResidualTransitionKind::TransferWalk:
                components.transfer_walk_time = transition.run_time;
                break;
            case ResidualTransitionKind::EgressWalk:
                components.egress_time = transition.run_time;
                break;
            case ResidualTransitionKind::FirstTimedRide:
                components.in_vehicle_time = transition.run_time;
                break;
            case ResidualTransitionKind::TransferTimedRide:
                components.in_vehicle_time = transition.run_time;
                components.transfer_count  = TransferCount{ 1 };
                break;
        }
        return connection_impedance_value(components, impedance, fare_scale);
    }

}  // namespace timetable::domain::assignment
