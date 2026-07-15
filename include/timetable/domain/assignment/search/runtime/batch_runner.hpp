#pragma once

#include <cstddef>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/search/diagnostics.hpp"
#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/od_day/supply_graph.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search/runtime/cancellation.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/params/search_params.hpp"

namespace timetable::domain::assignment::runtime::detail {

    [[nodiscard]] mathfp::Expected<std::vector<SearchSlotResult>>
    search_batch_connections(
          const SearchBatch&                 batch
        , const PreprocessedNetwork&         network
        , const ResidualReverseGraph&        reverse_graph
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan&  pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchPartialRetentionScope        partial_retention_scope
        , SearchDiagnosticsContext           diagnostics
        , std::size_t                        batch_index
        , std::size_t                        batch_count
        , const DayLevelSupplySearchGraph*   day_level_supply = nullptr
        , const SearchCancellationToken*     cancellation = nullptr
    );

}  // namespace timetable::domain::assignment::runtime::detail
