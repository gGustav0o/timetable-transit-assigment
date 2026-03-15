#include "timetable/domain/assignment/branch_state.hpp"

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
    namespace {

        bool is_first_branch_segment(
            const BranchState& state
        ) noexcept {
            return !state.current_arrival_time.has_value();
        }

        bool is_timed_segment(const ConnectionSegment& segment) noexcept {
            return segment.departure.has_value();
        }

        bool is_walk_segment(const ConnectionSegment& segment) noexcept {
            return !is_timed_segment(segment);
        }

        bool is_first_timed_branch_segment(
            const BranchState& state
            , const ConnectionSegment& candidate
        ) noexcept {
            return is_first_branch_segment(state) && is_timed_segment(candidate);
        }

        bool violates_same_trip_rule(
            const BranchState& state
            , const ConnectionSegment& candidate
        ) noexcept {
            return state.last_segment
                && forbids_transfer_to_same_trip(*state.last_segment, candidate);
        }

        Time transfer_wait_time(
            Time current_arrival_time
            , const ConnectionSegment& candidate
        ) noexcept {
            return Time{ candidate.departure->value() - current_arrival_time.value() };
        }

        bool wait_time_within_limits(
            Time wait_time
            , const TransferLimits& limits
        ) noexcept {
            return wait_time.value() >= limits.min_transfer_wait.value()
                && wait_time.value() <= limits.max_transfer_wait.value();
        }

        bool start_wait_allowed(
            const BranchState& state
            , const ConnectionSegment& candidate
            , const TransferLimits& limits
        ) noexcept {
            if (!is_first_timed_branch_segment(state, candidate)) {
                return true;
            }
            if (limits.allow_start_wait) {
                return true;
            }
            if (!state.start_time.has_value()) {
                return true;
            }
            return candidate.departure->value() <= state.start_time->value();
        }

    }  // namespace

    bool is_branch_extension_feasible(
        const BranchState& state
        , const ConnectionSegment& candidate
        , const TransferLimits& limits
    ) noexcept {
        // TODO: Enforce limits.max_transfers once real branch expansion updates
        // BranchState.transfer_count consistently for every extension step.
        if (!start_wait_allowed(state, candidate, limits)) {
            return false;
        }

        if (is_first_branch_segment(state)) {
            return true;
        }

        if (is_walk_segment(candidate)) {
            // Walk segments are always available and therefore do not consume transfer wait.
            return true;
        }

        if (violates_same_trip_rule(state, candidate)) {
            return false;
        }

        const auto wait_time = transfer_wait_time(*state.current_arrival_time, candidate);
        return wait_time_within_limits(wait_time, limits);
    }

}  // namespace timetable::domain::assignment
