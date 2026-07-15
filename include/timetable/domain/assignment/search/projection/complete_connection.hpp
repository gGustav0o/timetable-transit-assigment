#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    struct CompleteProjectionRetentionDiagnostics final {
        std::size_t completed_connections{};
        std::size_t rejected_complete_admissibility{};
        std::size_t rejected_complete_dominance{};
        std::size_t removed_complete_dominated{};
        std::size_t post_layer_day_path_candidates{};
        std::size_t post_layer_day_path_inserted{};
        std::size_t post_layer_day_path_representative_replaced{};
        std::size_t post_layer_day_path_supports{};
    };

    [[nodiscard]] std::vector<ConnectionSegmentId> branch_connection_segments(
          const BranchArena&  branches
        , const SearchBranch& branch
    );

    [[nodiscard]] mathfp::Expected<ConnectionTrace> materialize_connection_trace(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
    );

    [[nodiscard]] mathfp::Expected<std::optional<SearchConnection>> complete_connection(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const TransferLimits&      limits
        , ZoneId                     destination
    );

    [[nodiscard]] bool complete_branch_can_finish(
          const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const TransferLimits&      limits
        , ZoneId                     destination
    );

    [[nodiscard]] mathfp::Expected<CompleteConnectionMetrics>
    complete_connection_metrics_from_branch(
          const SearchBranch&       branch
        , const SearchCostContext&  search_cost
    );

    [[nodiscard]] std::size_t finalize_compact_complete_connection_count(
          const CompactCompleteConnectionRetention& retention
        , const ChoiceTolerances&                   tolerances
        , ChoiceRolloutStage                        rollout_stage
    ) noexcept;

    [[nodiscard]] mathfp::Expected<CompleteConnectionRetentionDecision>
    retain_exact_compact_complete_connection(
          CompactCompleteConnectionRetention& retention
        , const BranchArena&                  branches
        , const SearchBranch&                 branch
        , CompleteConnectionMetrics           metrics
        , const CompleteConnectionDominanceConfig& dominance_config
    );

    [[nodiscard]] mathfp::Expected<CompleteProjectionRetentionDiagnostics>
    retain_complete_projection_for_slot(
          const SearchBranch&        branch
        , const BranchArena&         branches
        , const PreprocessedNetwork& network
        , const TransferLimits&      transfer_limits
        , const SearchCostContext&   search_cost
        , const SearchProjectionSlot& slot
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const CompleteConnectionDominanceConfig& dominance_config
        , SearchProjectionRetention& retention
    );

}  // namespace timetable::domain::assignment
