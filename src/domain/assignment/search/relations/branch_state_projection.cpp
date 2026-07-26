#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"

#include <mathfp/core/checked_arithmetic.hpp>

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {

    SearchPruningTransferContext search_transfer_context(
        const SearchBranch& branch
    ) noexcept {
        return SearchPruningTransferContext{
              .last_trip = branch.trace.last_timed_segment != nullptr
                ? branch.trace.last_timed_segment->trip
                : std::optional<TripId>{}
            , .last_line = branch.trace.last_timed_route_segment != nullptr
                ? line_of(*branch.trace.last_timed_route_segment)
                : std::optional<LineId>{}
        };
    }

    std::optional<StopOccurrenceKey> search_last_timed_occurrence(
        const SearchBranch& branch
    ) noexcept {
        if (branch.trace.last_timed_route_segment == nullptr) {
            return std::nullopt;
        }
        return occurrence_key(
            line_topology_of(*branch.trace.last_timed_route_segment)->to
        );
    }

    SearchPruningStateProjection search_pruning_state_projection(
        const SearchBranch& branch
    ) noexcept {
        return SearchPruningStateProjection{
              .physical              = branch.trace.current_physical
            , .current_occurrence    = branch.trace.current_occurrence
            , .last_timed_occurrence = search_last_timed_occurrence(branch)
            , .phase                 = branch.trace.phase
            , .transfer              = search_transfer_context(branch)
        };
    }

    SearchNodeKey search_node_key(
          const SearchBranch&               branch
        , const SearchPruningExecutionPlan& pruning_execution
    ) noexcept {
        return make_search_pruning_state_key(
              search_pruning_state_projection(branch)
            , pruning_execution.equivalent_connection_dominance
        );
    }

    NodeConnectionSetKey node_connection_set_key(
        const SearchBranch& branch
    ) noexcept {
        return NodeConnectionSetKey{
            .physical = branch.trace.current_physical
        };
    }

    bool is_complete_connection(
          const SearchBranch& branch
        , ZoneId              task_destination
    ) noexcept {
        return branch.metrics.departure.has_value()
            && branch.trace.current_physical.kind == EndpointKind::Zone
            && branch.trace.current_physical.id   == task_destination.get();
    }

    SearchBranchPhase search_branch_phase(
          const SearchBranch& branch
        , ZoneId              task_destination
    ) noexcept {
        if (is_complete_connection(branch, task_destination)) {
            return SearchBranchPhase::Completed;
        }
        return branch.trace.phase;
    }

    TransferCount remaining_transfer_budget(
          const SearchBranch&   branch
        , const TransferLimits& limits
    ) noexcept {
        const auto used = branch.metrics.departure.has_value()
            ? branch.metrics.transfers
            : TransferCount{0};

        if (limits.max_transfers <= used) {
            return TransferCount{0};
        }

        const auto remaining = mathfp::checked_sub(limits.max_transfers.get(), used.get());
        return TransferCount{ *remaining };
    }

    RelaxedSuffixState relaxed_suffix_state(
          const SearchBranch&   branch
        , ZoneId                destination
        , const TransferLimits& limits
    ) noexcept {
        return RelaxedSuffixState{
              .current_physical    = branch.trace.current_physical
            , .destination         = destination
            , .phase               = search_branch_phase(branch, destination)
            , .remaining_transfers = remaining_transfer_budget(branch, limits)
        };
    }

    RelaxedSuffixState relaxed_suffix_state(
          const SearchBranch&   branch
        , const SearchTask&     task
        , const TransferLimits& limits
    ) noexcept {
        return relaxed_suffix_state(branch, task.destination, limits);
    }

}  // namespace timetable::domain::assignment
