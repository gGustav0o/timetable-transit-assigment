#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "timetable/domain/assignment/search/problem.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Phase of a branch in the timetable connection tree.
     *
     * The phase captures the structural constraints of the connection-tree
     * tree. Walk legs are atomic connection segments: access may occur only
     * before the first boarding, transfer walk may occur only after a timed
     * ride and cannot be chained with another walk leg, and completion occurs
     * only at the task destination zone.
     */
    enum class SearchBranchPhase : std::uint8_t {
          AtOrigin
        , BeforeFirstBoarding
        , AfterTimedRide
        , AfterTransferWalk
        , Completed
    };

    /**
     * @brief State used by timetable-independent suffix reachability.
     *
     * This is an intentionally relaxed model of the remaining search problem.
     * It ignores concrete departures, arrivals and waiting times. Therefore, if
     * a suffix is impossible in this model, it is also impossible in the full
     * timetable search. The converse is not required.
     */
    struct RelaxedSuffixState final {
        EndpointKey       current_physical{};
        ZoneId            destination;
        SearchBranchPhase phase{ SearchBranchPhase::AtOrigin };
        TransferCount     remaining_transfers;
    };

    enum class ReachabilityRejectionReason : std::uint8_t {
          Phase
        , TransferBudget
        , UnreachableDestination
    };

    struct ResidualReachabilityKey final {
        EndpointKey       current_physical{};
        SearchBranchPhase phase{ SearchBranchPhase::AtOrigin };
        TransferCount     remaining_transfers;
    };

    [[nodiscard]] bool operator==(
          const ResidualReachabilityKey& lhs
        , const ResidualReachabilityKey& rhs
    ) noexcept;

    struct ResidualReachabilityKeyHash final {
        std::size_t operator()(const ResidualReachabilityKey& key) const noexcept;
    };

    struct ResidualSuffixLowerBounds final {
        Time          journey_time{};
        TransferCount transfers{ TransferCount{0} };
        double        impedance{};
    };

    using ResidualSuffixLowerBoundMap = std::unordered_map<
          ResidualReachabilityKey
        , ResidualSuffixLowerBounds
        , ResidualReachabilityKeyHash
    >;

    using ResidualReachabilityStateSet = std::unordered_set<
          ResidualReachabilityKey
        , ResidualReachabilityKeyHash
    >;

    struct DestinationResidualReachability final {
        ResidualReachabilityStateSet reachable_states{};
        ResidualSuffixLowerBoundMap suffix_lower_bounds{};
    };

    struct ResidualReachability final {
        std::map<ZoneId, DestinationResidualReachability> destinations{};
    };

    struct ReachabilityDecision final {
        bool                        feasible{};
        ReachabilityRejectionReason rejection_reason{ ReachabilityRejectionReason::UnreachableDestination };
    };

    [[nodiscard]] constexpr bool has_timed_ride(
        SearchBranchPhase phase
    ) noexcept {
        return phase == SearchBranchPhase::AfterTimedRide
            || phase == SearchBranchPhase::AfterTransferWalk
            || phase == SearchBranchPhase::Completed;
    }

    [[nodiscard]] constexpr bool is_completed(
        SearchBranchPhase phase
    ) noexcept {
        return phase == SearchBranchPhase::Completed;
    }

    [[nodiscard]] ResidualReachabilityKey residual_reachability_key(
        const RelaxedSuffixState& state
    ) noexcept;

    [[nodiscard]] bool has_reachable_state(
          const DestinationResidualReachability& destination
        , const ResidualReachabilityKey&         state
    ) noexcept;

}  // namespace timetable::domain::assignment
