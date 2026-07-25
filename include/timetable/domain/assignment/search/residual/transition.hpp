#pragma once

#include <cstdint>
#include <vector>

#include "timetable/domain/assignment/search/residual/graph.hpp"
#include "timetable/domain/assignment/search/residual/types.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    enum class ResidualTransitionKind : std::uint8_t {
          AccessWalk
        , TransferWalk
        , EgressWalk
        , FirstTimedRide
        , TransferTimedRide
    };

    struct ResidualPredecessorTransition final {
        ResidualReachabilityKey predecessor{};
        Time                    run_time{};
        ResidualTransitionKind  kind{ ResidualTransitionKind::AccessWalk };
    };

    [[nodiscard]] std::vector<ResidualPredecessorTransition> residual_predecessor_transitions(
          const ResidualReverseGraph&   graph
        , const ResidualReachabilityKey& state
        , TransferCount                  max_transfers
    );

    [[nodiscard]] double residual_transition_journey_time(
        const ResidualPredecessorTransition& transition
    ) noexcept;

    [[nodiscard]] double residual_transition_transfer_count(
        const ResidualPredecessorTransition& transition
    ) noexcept;

    [[nodiscard]] double residual_transition_impedance(
          const ResidualPredecessorTransition& transition
        , const SearchImpedance&               impedance
        , double                               fare_scale
    ) noexcept;

}  // namespace timetable::domain::assignment
