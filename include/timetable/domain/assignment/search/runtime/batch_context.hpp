#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/diagnostics.hpp"
#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/od_day/supply_graph.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/projection/sink.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search/runtime/cancellation.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/assignment/search/runtime/reachability_masks.hpp"
#include "timetable/domain/assignment/search/tree/level_expansion.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment::runtime {

    struct SearchBatchStaticContext final {
        const SearchBatch& batch;
        const PreprocessedNetwork& network;
        const ResidualReverseGraph& reverse_graph;
        const SearchParams& params;
        const SearchCostContext& search_cost;
        const ChoiceConfig& choice_config;
        const AssignmentPeriodConfig& assignment_period;
        const ConnectionAdmissibilityConfig& admissibility_config;
        const SearchPruningExecutionPlan& pruning_execution;
        const CompleteConnectionDominanceConfig& complete_connection_dominance;
        SearchPartialRetentionScope partial_retention_scope;
        SearchDiagnosticsContext diagnostics;
        std::size_t batch_index{};
        std::size_t batch_count{};
        const DayLevelSupplySearchGraph* day_level_supply{};
        const DayLevelSupplySearchGraph* od_day_supply{};
        const SearchCancellationToken* cancellation{};
        const SearchTimeDomain* first_departure_domain{};
        std::span<const SearchProjectionSlot> batch_tasks{};
        std::span<const SearchCompletionTarget> batch_targets{};
        SearchProjectionSinkSet projection_sinks{};
        bool target_projection_slots{};
        bool od_day_slots{};
        std::map<ZoneId, std::size_t> target_positions_by_destination{};
        const std::map<ZoneId, std::size_t>* projection_positions_by_destination{};
        const std::unordered_set<std::int64_t>* od_day_destination_ids{};
    };

    struct SearchBatchMutableState final {
        std::vector<SearchProjectionRetention>& retentions;
        TreePartialRetention& tree_partial_retention;
        PaperConnectionLabelRegistry& paper_label_registry;
        TaskSearchStats& stats;
        std::vector<TaskSearchStats>& task_stats;
        BranchArena& branches;
        std::deque<std::optional<FixedActiveMask>>& completion_projection_states;
        std::deque<std::optional<DemandBranchProjectionState>>& demand_projection_states;
        std::size_t& released_branches;
        std::optional<ResidualReachability>& reachability;
        std::optional<ReachabilityMaskCache>& reachability_cache;
        SearchLevelExpansion& level_expansion;
    };

    struct SearchBatchContext final {
        SearchBatchStaticContext fixed;
        SearchBatchMutableState mutable_state;
    };

}  // namespace timetable::domain::assignment::runtime
