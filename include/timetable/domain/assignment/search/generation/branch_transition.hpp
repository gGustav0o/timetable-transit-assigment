#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/params/transfer_limits.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] bool branch_revisits_physical(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , EndpointKey                next
    );

    [[nodiscard]] bool branch_revisits_occurrence(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , StopOccurrenceKey          next
    );

    [[nodiscard]] CapacityExposure add_capacity_exposure(
          CapacityExposure lhs
        , CapacityExposure rhs
    ) noexcept;

    [[nodiscard]] mathfp::Expected<CapacityExposure> timed_successor_capacity_exposure(
          const ConnectionSegment& segment
        , const RouteSegment&      route_segment
        , std::optional<IntervalId> interval
        , const SearchCostContext& search_cost
    );

    [[nodiscard]] SearchPartialMetrics extend_metrics_with_walk(
          SearchPartialMetrics metrics
        , const RouteSegment&   route_segment
        , ConnectionLegKind     kind
    );

    [[nodiscard]] mathfp::Expected<SearchPartialMetrics> extend_metrics_with_timed(
          SearchPartialMetrics      metrics
        , const ConnectionSegment&  segment
        , CapacityExposure          capacity_exposure
    );

    enum class BranchTransitionRejection : std::uint8_t {
          None
        , MissingWalkTransition
        , RepeatedPhysicalNode
        , InvalidTimedPhase
        , RepeatedStopOccurrence
    };

    enum class PaperSuccessorFeasibilityRejection : std::uint8_t {
          None
        , FirstDepartureDomain
        , BranchFeasibility
        , Reboarding
    };

    struct BranchTransitionDiagnostics final {
        BranchTransitionRejection rejection{ BranchTransitionRejection::None };

        [[nodiscard]] bool accepted() const noexcept {
            return rejection == BranchTransitionRejection::None;
        }
    };

    struct BranchTransitionResult final {
        std::optional<SearchBranch>       branch{};
        BranchTransitionDiagnostics       diagnostics{};
    };

    enum class PaperConnectionPrefixKind : std::uint8_t {
          UntimedAccessPrefix
        , ConnectionPrefix
    };

    struct PaperConnectionCandidateMetrics final {
        PaperConnectionNodeKey node{};
        SearchPruningMetrics   metrics{};
    };

    struct PaperConnectionPrefixEvaluation final {
        PaperConnectionPrefixKind kind{ PaperConnectionPrefixKind::UntimedAccessPrefix };
        std::optional<PaperConnectionCandidateMetrics> connection_candidate{};
        BranchTransitionRejection rejection{ BranchTransitionRejection::None };

        [[nodiscard]] bool accepted() const noexcept {
            return rejection == BranchTransitionRejection::None;
        }
    };

    struct PaperSuccessorFeasibilityDecision final {
        PaperSuccessorFeasibilityRejection rejection{
            PaperSuccessorFeasibilityRejection::None
        };

        [[nodiscard]] bool accepted() const noexcept {
            return rejection == PaperSuccessorFeasibilityRejection::None;
        }
    };

    [[nodiscard]] PaperSuccessorFeasibilityDecision evaluate_paper_search_successor_feasibility(
          const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor
        , const SearchTimeDomain*    first_departure_domain
        , const TransferLimits&      limits
    ) noexcept;

    [[nodiscard]] mathfp::Expected<PaperConnectionPrefixEvaluation>
    evaluate_paper_connection_prefix_before_branch(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor
        , std::optional<IntervalId>  interval
        , const SearchCostContext&   search_cost
    );

    [[nodiscard]] mathfp::Expected<BranchTransitionResult> transition_search_branch_with_diagnostics(
          const BranchArena&         branches
        , std::size_t                branch_index
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor
        , std::optional<IntervalId>  interval
        , const SearchCostContext&   search_cost
    );

    [[nodiscard]] mathfp::Expected<std::optional<SearchBranch>> transition_search_branch(
          const BranchArena&         branches
        , std::size_t                branch_index
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor
        , std::optional<IntervalId>  interval
        , const SearchCostContext&   search_cost
    );

}  // namespace timetable::domain::assignment
