#include "timetable/domain/assignment/search/branch_state.hpp"

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
              const BranchState&       state
            , const ConnectionSegment& candidate
        ) noexcept {
            return is_first_branch_segment(state) && is_timed_segment(candidate);
        }

        bool violates_same_trip_rule(
              const BranchState&       state
            , const ConnectionSegment& candidate
        ) noexcept {
            return state.last_segment
                && transfer_reuses_same_trip(*state.last_segment, candidate);
        }

        bool is_stop_endpoint(const WalkEndpoint& endpoint) noexcept {
            return std::holds_alternative<StopId>(endpoint);
        }

        bool same_transfer_stop(
              const RouteSegment& current
            , const RouteSegment& candidate
        ) noexcept {
            const auto current_to     = physical_to_endpoint(current);
            const auto candidate_from = physical_from_endpoint(candidate);
            if (!is_stop_endpoint(current_to) || !is_stop_endpoint(candidate_from)) {
                return false;
            }
            return std::get<StopId>(current_to) == std::get<StopId>(candidate_from);
        }

        bool is_repeated_stop_reboarding_transfer(
              const BranchState&       state
            , const ConnectionSegment& candidate
            , const RouteSegment&      candidate_route_segment
        ) noexcept {
            // TODO: ?
            if (!state.last_segment || !state.last_route_segment) {
                return false;
            }
            if (!same_line(*state.last_route_segment, candidate_route_segment)) {
                return false;
            }
            if (!same_transfer_stop(*state.last_route_segment, candidate_route_segment)) {
                return false;
            }
            if (!state.last_segment->to_index.has_value() || !candidate.from_index.has_value()) {
                return false;
            }

            // The same physical stop may appear at multiple route positions on
            // a line. Reboarding is only meaningful if the candidate boards the
            // same stop at an earlier route position.
            return candidate.from_index.value() < state.last_segment->to_index.value();
        }

        bool violates_same_line_transfer_rule(
              const BranchState&       state
            , const ConnectionSegment& candidate
            , const RouteSegment&      candidate_route_segment
        ) noexcept {
            if (!state.last_segment || !state.last_route_segment) {
                return false;
            }
            if (!same_line(*state.last_route_segment, candidate_route_segment)) {
                return false;
            }
            return !is_repeated_stop_reboarding_transfer(state, candidate, candidate_route_segment);
        }

        Time transfer_wait_time(
              Time                     current_arrival_time
            , const ConnectionSegment& candidate
        ) noexcept {
            return Time{ candidate.departure->value() - current_arrival_time.value() };
        }

        bool wait_time_within_limits(
              Time                  wait_time
            , const TransferLimits& limits
        ) noexcept {
            return wait_time.value() >= limits.min_transfer_wait.value()
                && wait_time.value() <= limits.max_transfer_wait.value();
        }

        bool start_wait_allowed(
              const BranchState&       state
            , const ConnectionSegment& candidate
            , const TransferLimits&    limits
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

        bool transfer_count_within_limits(
              const BranchState&       state
            , const ConnectionSegment& candidate
            , const TransferLimits&    limits
        ) noexcept {
            if (is_first_branch_segment(state) || is_walk_segment(candidate)) {
                return true;
            }

            if (!state.transfer_count.has_value()) {
                return true;
            }

            return state.transfer_count->get() < limits.max_transfers.get();
        }

    }  // namespace

    bool is_branch_extension_feasible(
          const BranchState&       state
        , const ConnectionSegment& candidate
        , const RouteSegment&      candidate_route_segment
        , const TransferLimits&    limits
    ) noexcept {
        if (!start_wait_allowed(state, candidate, limits)) {
            return false;
        }

        if (!transfer_count_within_limits(state, candidate, limits)) {
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

        if (violates_same_line_transfer_rule(state, candidate, candidate_route_segment)) {
            return false;
        }

        const auto wait_time = transfer_wait_time(*state.current_arrival_time, candidate);
        return wait_time_within_limits(wait_time, limits);
    }

}  // namespace timetable::domain::assignment
