#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] std::size_t remove_inactive_node_connection_metrics(
          ConnectionSetCy&                    set
        , const RetainedConnectionLabelRegistry& registry
    );

    [[nodiscard]] std::size_t remove_inactive_node_connection_sets(
          NodeConnectionSetMap&       retention
        , const RetainedConnectionLabelRegistry& registry
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_retained_connection_label_sync(
          const NodeConnectionSetMap& retention
        , const RetainedConnectionLabelRegistry& registry
        , ZoneId                              origin
    );

    void update_node_connection_summary_with_metrics(
          SearchPruningSummary&       summary
        , const SearchPruningMetrics& metrics
    ) noexcept;

    void insert_node_connection_metrics(
          const SearchPruningExecutionPlan& pruning_execution
        , ConnectionSetCy&                  set
        , SearchPruningMetrics              metrics
        , RetainedConnectionLabelId            label
        , std::vector<RetainedConnectionLabelId>& removed_labels
    );

    [[nodiscard]] mathfp::Expected<SearchPruningDecision> retain_branch(
          const SearchBranch&               branch
        , NodeMetricMap&                    known_metrics
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    );

    [[nodiscard]] mathfp::Expected<SearchPruningDecision> retain_branch(
          const SearchBranch&               branch
        , SearchProjectionRetention&        retention
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    );

    [[nodiscard]] mathfp::Expected<SearchPruningDecision> retain_branch(
          const SearchBranch&               branch
        , TreePartialRetention&             retention
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const SearchPruningExecutionPlan& pruning_execution
        , SearchPruningRuntimeStats&        pruning_stats
    );

    [[nodiscard]] bool same_pruning_metrics(
          const SearchPruningMetrics& lhs
        , const SearchPruningMetrics& rhs
    ) noexcept;

    [[nodiscard]] bool contains_pruning_metrics(
          const NodeMetricSet&        metric_set
        , const SearchPruningMetrics& metrics
    ) noexcept;

}  // namespace timetable::domain::assignment
