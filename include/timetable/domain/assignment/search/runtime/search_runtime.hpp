#pragma once

#include "timetable/domain/assignment/search/search.hpp"

namespace timetable::domain::assignment::runtime {

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    );

    mathfp::Expected<AllZoneConnectionSearchResult> search_all_zone_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    );

    mathfp::Expected<mathfp::Unit> search_od_day_paths_by_origin_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , OdDayOriginResultSink      origin_sink
        , SearchDiagnosticsContext diagnostics
    );

}  // namespace timetable::domain::assignment::runtime
