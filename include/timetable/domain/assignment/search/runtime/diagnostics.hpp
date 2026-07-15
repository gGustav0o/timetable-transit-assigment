#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/pruning/suffix_lower_bound.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search/tree/level_expansion.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment::runtime {

    struct ReachabilityRejectionStats final {
        std::size_t phase{};
        std::size_t transfer_budget{};
        std::size_t unreachable_destination{};
    };

    struct WalkKindStats final {
        std::size_t access{};
        std::size_t transfer{};
        std::size_t egress{};
    };

    struct WalkLookupStats final {
        std::size_t access{};
        std::size_t transfer{};
        std::size_t egress{};
        std::size_t skipped_by_phase{};
        std::size_t skipped_by_transfer_budget{};
    };

    struct SuffixLowerBoundRejectionStats final {
        std::size_t exact_dominance{};
        std::size_t tolerance_impedance{};
        std::size_t tolerance_journey_time{};
        std::size_t tolerance_transfers{};
    };

    struct TaskSearchStats final {
        std::size_t expanded_branches{};
        std::size_t generated_successors{};
        std::size_t accepted_branches{};
        WalkLookupStats walk_lookup{};
        WalkKindStats generated_walk{};
        WalkKindStats accepted_walk{};
        std::size_t rejected_consecutive_walk{};
        std::size_t stale_frontier_skipped{};
        std::size_t c_y_removed_dominated{};
        std::size_t c_y_removed_stale{};
        std::size_t frontier_compaction_runs{};
        std::size_t frontier_compaction_removed{};
        std::size_t frontier_compaction_removed_current{};
        std::size_t frontier_compaction_removed_next{};
        std::size_t post_layer_day_path_candidates{};
        std::size_t post_layer_day_path_inserted{};
        std::size_t post_layer_day_path_representative_replaced{};
        std::size_t post_layer_day_path_supports{};
        std::size_t paper_timed_lookup_skipped_phase{};
        std::size_t paper_timed_lookup_skipped_transfer_budget{};
        std::size_t paper_timed_successor_rejected_time_domain{};
        std::size_t paper_timed_successor_rejected_same_trip{};
        std::size_t paper_timed_successor_rejected_same_line{};
        std::size_t paper_timed_successor_rejected_feasibility{};
        BranchPhaseStats accepted_branches_by_phase{};
        std::size_t rejected_time_domain{};
        std::size_t rejected_feasibility{};
        std::size_t rejected_reboarding{};
        std::size_t rejected_cycles{};
        std::size_t rejected_transfer_limit{};
        std::size_t rejected_reachability{};
        ReachabilityRejectionStats reachability_rejections{};
        std::size_t rejected_suffix_lower_bound{};
        SuffixLowerBoundRejectionStats suffix_lower_bound_rejections{};
        std::size_t rejected_dominance_or_tolerance{};
        std::size_t completed_connections{};
        std::size_t rejected_complete_admissibility{};
        std::size_t rejected_complete_dominance{};
        std::size_t removed_complete_dominated{};
        std::size_t rejected_complete_tolerance{};
        std::size_t max_current_frontier{};
        std::size_t max_next_frontier{};
        SearchPruningRuntimeStats pruning{};
    };

    struct SearchStorageDiagnostics final {
        std::size_t branch_slots{};
        std::size_t live_branches{};
        std::size_t released_branches{};
        std::size_t projection_states{};
        std::size_t tree_pruning_nodes{};
        std::size_t tree_pruning_buckets{};
        float       tree_pruning_load_factor{};
        std::size_t tree_pruning_metrics{};
        std::size_t tree_pruning_labels{};
        std::size_t od_day_label_state_nodes{};
        std::size_t od_day_label_state_buckets{};
        float       od_day_label_state_load_factor{};
        std::size_t od_day_label_representatives{};
        std::size_t tree_pruning_insertions{};
        std::size_t retained_complete_connections{};
        std::size_t retained_day_paths{};
        std::size_t approximate_direct_bytes{};
    };

    struct OdDayProductionMemoryLimits final {
        std::optional<std::size_t> max_branch_slots_per_tree{};
        std::optional<std::size_t> max_live_branches_per_tree{};
        std::optional<std::size_t> max_frontier_per_tree{};
        std::optional<std::size_t> max_od_day_label_states_per_tree{};
        std::optional<std::size_t> max_retained_day_paths_per_tree{};
        std::optional<std::size_t> max_approximate_direct_bytes_per_tree{};
    };

    void increment_walk_kind_stats(
          WalkKindStats&    stats
        , ConnectionLegKind kind
    ) noexcept;

    [[nodiscard]] std::string format_walk_kind_stats(
        const WalkKindStats& stats
    );

    [[nodiscard]] std::string format_walk_lookup_stats(
        const WalkLookupStats& stats
    );

    [[nodiscard]] std::string format_branch_phase_stats(
        const BranchPhaseStats& stats
    );

    [[nodiscard]] std::size_t reachability_rejection_count(
        const ReachabilityRejectionStats& stats
    ) noexcept;

    [[nodiscard]] std::size_t suffix_lower_bound_rejection_count(
        const SuffixLowerBoundRejectionStats& stats
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_reachability_rejection_stats(
        const TaskSearchStats& stats
    );

    mathfp::Expected<mathfp::Unit> validate_suffix_lower_bound_rejection_stats(
        const TaskSearchStats& stats
    );

    void add_reachability_rejection(
          TaskSearchStats&            stats
        , ReachabilityRejectionReason reason
    ) noexcept;

    void add_suffix_lower_bound_rejection(
          TaskSearchStats&                 stats
        , SuffixLowerBoundRejectionReason reason
    ) noexcept;

    [[nodiscard]] std::size_t retained_connection_count(
        std::span<const SearchProjectionRetention> retentions
    ) noexcept;

    [[nodiscard]] std::size_t retained_day_path_count(
        std::span<const SearchProjectionRetention> retentions
    ) noexcept;

    [[nodiscard]] SearchStorageDiagnostics search_storage_diagnostics(
          const BranchArena&                         branches
        , std::size_t                                released_branches
        , std::size_t                                projection_state_count
        , std::size_t                                projection_state_size
        , const TreePartialRetention&                tree_retention
        , std::span<const SearchProjectionRetention> retentions
        , const SearchPruningRuntimeStats&           pruning_stats
    ) noexcept;

    [[nodiscard]] std::string format_search_storage_diagnostics(
        const SearchStorageDiagnostics& diagnostics
    );

    [[nodiscard]] std::string format_od_day_theory_diagnostics(
          const SearchStorageDiagnostics& diagnostics
        , const TaskSearchStats&          stats
        , std::size_t                     current_frontier
        , std::size_t                     next_frontier
    );

    mathfp::Expected<mathfp::Unit> validate_od_day_production_batch_invariants(
          const SearchBatch&               batch
        , const TaskSearchStats&           stats
        , const SearchStorageDiagnostics&  storage
    );

    mathfp::Expected<mathfp::Unit> validate_od_day_production_memory_limits(
          const SearchStorageDiagnostics& diagnostics
        , std::size_t                     current_frontier
        , std::size_t                     next_frontier
        , const OdDayProductionMemoryLimits& limits
        , ZoneId                          origin
    );

}  // namespace timetable::domain::assignment::runtime
