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

    [[nodiscard]] std::size_t remove_inactive_paper_node_connection_metrics(
          ConnectionSetCy&                    set
        , const PaperConnectionLabelRegistry& registry
    );

    [[nodiscard]] std::size_t remove_inactive_paper_connection_metrics(
          PaperConnectionNodeMetricMap&       retention
        , const PaperConnectionLabelRegistry& registry
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_paper_connection_label_sync(
          const PaperConnectionNodeMetricMap& retention
        , const PaperConnectionLabelRegistry& registry
        , ZoneId                              origin
    );

    void update_paper_node_summary_with_metrics(
          SearchPruningSummary&       summary
        , const SearchPruningMetrics& metrics
    ) noexcept;

    void insert_paper_node_connection_metrics(
          const SearchPruningExecutionPlan& pruning_execution
        , ConnectionSetCy&                  set
        , SearchPruningMetrics              metrics
        , PaperConnectionLabelId            label
        , std::vector<PaperConnectionLabelId>& removed_labels
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

    [[nodiscard]] bool contains_od_day_label_representative(
          const OdDayLabelRepresentativeSet& representatives
        , const SearchPruningMetrics&        metrics
        , const TimedSupportEnvelope&        support
    ) noexcept;

    [[nodiscard]] bool timed_support_envelope_covers(
          const TimedSupportEnvelope& existing
        , const TimedSupportEnvelope& candidate
    ) noexcept;

    [[nodiscard]] SearchPruningMetricSet compatible_od_day_label_metrics(
          const OdDayLabelRepresentativeSet& representatives
        , const TimedSupportEnvelope&        support
    );

    void insert_od_day_label_representative(
          const SearchPruningExecutionPlan& pruning_execution
        , OdDayLabelRepresentativeSet&      representatives
        , const SearchPruningMetrics&       metrics
        , const TimedSupportEnvelope&       support
    );

}  // namespace timetable::domain::assignment
