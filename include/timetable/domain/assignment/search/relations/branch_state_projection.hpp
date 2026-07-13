#pragma once

#include <optional>

#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] SearchPruningTransferContext search_transfer_context(
        const SearchBranch& branch
    ) noexcept;

    [[nodiscard]] std::optional<StopOccurrenceKey> search_last_timed_occurrence(
        const SearchBranch& branch
    ) noexcept;

    [[nodiscard]] SearchPruningStateProjection search_pruning_state_projection(
        const SearchBranch& branch
    ) noexcept;

    [[nodiscard]] SearchNodeKey search_node_key(
          const SearchBranch&               branch
        , const SearchPruningExecutionPlan& pruning_execution
    ) noexcept;

    [[nodiscard]] PaperConnectionNodeKey paper_connection_node_key(
        const SearchBranch& branch
    ) noexcept;

    [[nodiscard]] StructuralLabelState structural_label_state(
        const SearchBranch& branch
    ) noexcept;

    [[nodiscard]] OdDayLabelState od_day_label_state(
        const SearchBranch& branch
    ) noexcept;

    [[nodiscard]] bool is_complete_connection(
          const SearchBranch& branch
        , ZoneId              task_destination
    ) noexcept;

    [[nodiscard]] SearchBranchPhase search_branch_phase(
          const SearchBranch& branch
        , ZoneId              task_destination
    ) noexcept;

    [[nodiscard]] TransferCount remaining_transfer_budget(
          const SearchBranch&   branch
        , const TransferLimits& limits
    ) noexcept;

    [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
          const SearchBranch&   branch
        , ZoneId                destination
        , const TransferLimits& limits
    ) noexcept;

    [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
          const SearchBranch&   branch
        , const SearchTask&     task
        , const TransferLimits& limits
    ) noexcept;

}  // namespace timetable::domain::assignment
