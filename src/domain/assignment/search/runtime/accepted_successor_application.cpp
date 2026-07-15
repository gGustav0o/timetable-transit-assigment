#include "timetable/domain/assignment/search/runtime/accepted_successor_application.hpp"

#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"
#include "timetable/domain/assignment/search/runtime/branch_enqueue.hpp"
#include "timetable/domain/assignment/search/runtime/continuation_filter.hpp"
#include "timetable/domain/assignment/search/runtime/projection_application.hpp"
#include "timetable/domain/assignment/validation.hpp"

namespace timetable::domain::assignment::runtime {

    mathfp::Expected<AcceptedSuccessorApplicationResult>
    apply_accepted_successor(
          SearchBatchContext&    context
        , const SearchBranch&    parent_branch
        , const SearchSuccessor& successor_ref
        , const ConnectionSegment& connection
        , SearchBranch           candidate
        , const ActiveIndexSet&  active_tasks
        , const ActiveIndexSet&  active_targets
    ) {
        const auto& fixed = context.fixed;
        auto& state = context.mutable_state;

        candidate.od_day_carrier = project_od_day_carrier_transition(
              parent_branch.od_day_carrier
            , successor_ref
            , connection
            , route_segment_at(fixed.network, connection.route_segment)
        );
        if (fixed.od_day_slots) {
            /*
             * Production OD-day candidates carry structural path and support
             * witness in compact prefixes. The runtime application boundary
             * must not retain raw timed parent chains for this contour.
             */
            candidate.trace.parent_branch = std::nullopt;
        }

        if (fixed.diagnostics.validate_phase_invariants) {
            MATHFP_TRY(validate_search_branch_phase_invariants(candidate));
        }

        if (candidate.metrics.transfers > fixed.params.transfers.max_transfers) {
            ++state.stats.rejected_transfer_limit;
            return AcceptedSuccessorApplicationResult{
                .outcome = AcceptedSuccessorApplicationOutcome::Rejected
            };
        }

        auto projection_result = apply_projection_sink(
              context
            , candidate
            , successor_ref
            , active_tasks
            , active_targets
        );
        if (!projection_result) {
            return mathfp::unexpected(std::move(projection_result.error()));
        }
        if (projection_result->outcome
            == ProjectionApplicationOutcome::CompletedProjection) {
            return AcceptedSuccessorApplicationResult{
                .outcome = AcceptedSuccessorApplicationOutcome::CompletedProjection
            };
        }
        if (projection_result->outcome
            == ProjectionApplicationOutcome::RejectedZoneSink) {
            return AcceptedSuccessorApplicationResult{
                .outcome = AcceptedSuccessorApplicationOutcome::Rejected
            };
        }

        if (fixed.od_day_slots) {
            const auto candidate_index = enqueue_od_day_branch(
                  context
                , std::move(candidate)
                , successor_ref
            );
            return AcceptedSuccessorApplicationResult{
                  .outcome = AcceptedSuccessorApplicationOutcome::Enqueued
                , .enqueued_branch_index = candidate_index
            };
        }

        auto continuation = filter_continuation_projection(
              context
            , candidate
            , active_tasks
            , active_targets
        );
        if (!continuation) {
            return mathfp::unexpected(std::move(continuation.error()));
        }
        if (!continuation->has_value()) {
            return AcceptedSuccessorApplicationResult{
                .outcome = AcceptedSuccessorApplicationOutcome::Rejected
            };
        }

        const auto candidate_index = enqueue_projected_branch(
              context
            , parent_branch
            , connection
            , std::move(candidate)
            , successor_ref
            , std::move(**continuation)
        );
        return AcceptedSuccessorApplicationResult{
              .outcome = AcceptedSuccessorApplicationOutcome::Enqueued
            , .enqueued_branch_index = candidate_index
        };
    }

}  // namespace timetable::domain::assignment::runtime
