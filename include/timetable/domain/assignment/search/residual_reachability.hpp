#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/problem.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Phase of a branch in the timetable connection tree.
     *
     * The phase captures the structural constraints of the paper's connection
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

    struct ResidualReverseEdge final {
        EndpointKey predecessor{};
        Time        run_time{};
    };

    struct ResidualReverseGraph final {
        std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> walk_predecessors_by_node{};
        std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> timed_predecessors_by_node{};
    };

    struct ResidualSuffixLowerBounds final {
        Time          journey_time{};
        TransferCount transfers{ TransferCount{0} };
        double        impedance{};
    };

    struct DestinationResidualReachability final {
        std::unordered_set<ResidualReachabilityKey, ResidualReachabilityKeyHash> reachable_states{};
        std::unordered_map<
              ResidualReachabilityKey
            , ResidualSuffixLowerBounds
            , ResidualReachabilityKeyHash
        > suffix_lower_bounds{};
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

    [[nodiscard]] ResidualReverseGraph build_residual_reverse_graph(
          std::span<const RouteSegment>      route_segments
        , std::span<const ConnectionSegment> connection_segments
    );

    [[nodiscard]] bool has_reachable_state(
          const DestinationResidualReachability& destination
        , const ResidualReachabilityKey&         state
    ) noexcept;

    [[nodiscard]] ResidualReachability build_residual_reachability(
          const ResidualReverseGraph& graph
        , std::span<const SearchCompletionTarget> targets
        , TransferCount               max_transfers
        , const SearchImpedance&      impedance
        , double                      fare_scale
    );

    mathfp::Expected<mathfp::Unit> validate_residual_reachability(
          const ResidualReachability& reachability
        , TransferCount               max_transfers
    );

    [[nodiscard]] ReachabilityDecision evaluate_residual_reachability(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept;

    [[nodiscard]] bool can_have_feasible_suffix(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept;

}  // namespace timetable::domain::assignment
