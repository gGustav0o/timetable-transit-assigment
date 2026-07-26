#include "timetable/domain/assignment/search/branch_state.hpp"

#include <cstddef>

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

        std::optional<std::pair<std::size_t, std::size_t>> timed_bucket_range(
              const preprocessing::ConnectionSegmentIndex& index
            , StopOccurrenceKey                            from
        ) {
            const auto bucket = preprocessing::find_bucket(index.timed_buckets, from);
            if (!bucket) {
                return std::nullopt;
            }

            const auto i = *bucket;
            return std::pair<std::size_t, std::size_t>{
                  index.timed_offsets[i]
                , index.timed_offsets[i + 1]
            };
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

        bool same_line_route(
              const RouteSegment& current
            , const RouteSegment& candidate
        ) noexcept {
            const auto* current_line  = line_topology_of(current);
            const auto* candidate_line = line_topology_of(candidate);
            return current_line != nullptr
                && candidate_line != nullptr
                && current_line->line == candidate_line->line
                && current_line->route == candidate_line->route;
        }

        bool is_repeated_stop_reboarding_transfer(
              const BranchState&       state
            , const ConnectionSegment& candidate
            , const RouteSegment&      candidate_route_segment
        ) noexcept {
            if (!state.last_segment || !state.last_route_segment) {
                return false;
            }
            if (!same_line_route(*state.last_route_segment, candidate_route_segment)) {
                return false;
            }
            if (!same_transfer_stop(*state.last_route_segment, candidate_route_segment)) {
                return false;
            }
            if (!state.last_segment->to_index.has_value() || !candidate.from_index.has_value()) {
                return false;
            }

            // The same physical stop may appear at multiple route positions on
            // a loop line. The loop-line earlier-service-trip case means that
            // the boarded vehicle is already farther along the loop at the same
            // physical stop. Thus the route-position axis stays linear: after
            // reaching an occurrence, the search must not wrap to an earlier one.
            return state.last_segment->to_index.value() < candidate.from_index.value();
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

        std::optional<Time> same_trip_continuation_arrival(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , const RouteSegment&        successor_route_segment
        ) noexcept {
            if (!branch.trace.last_timed_segment || !branch.trace.last_timed_route_segment) {
                return std::nullopt;
            }
            if (
                   !branch.trace.last_timed_segment->trip    .has_value()
                || !branch.trace.last_timed_segment->to_index.has_value()
                || !successor.to_index                    .has_value()
            ) {
                return std::nullopt;
            }

            const auto current_stop = occurrence_key(
                line_topology_of(*branch.trace.last_timed_route_segment)->to
            );
            const auto desired_route_to_index = successor.to_index.value();
            const auto range = timed_bucket_range(network.connection_index, current_stop);
            if (!range) {
                return std::nullopt;
            }

            const auto [start, end] = *range;
            for (std::size_t i = start; i < end; ++i) {
                const auto connection_id = network.connection_index.timed_order[i];
                const auto& continuation = connection_segment_at(network, connection_id);
                const auto& continuation_route_segment = route_segment_at(
                      network
                    , continuation.route_segment
                );
                const auto* continuation_line = line_topology_of(continuation_route_segment);
                const auto* current_line = line_topology_of(*branch.trace.last_timed_route_segment);
                if (!continuation_line || !current_line) {
                    continue;
                }
                if (continuation_line->line != current_line->line
                    || continuation_line->route != current_line->route) {
                    continue;
                }
                if (continuation.trip != branch.trace.last_timed_segment->trip) {
                    continue;
                }
                if (continuation.from_index != branch.trace.last_timed_segment->to_index) {
                    continue;
                }
                if (continuation.to_index != desired_route_to_index) {
                    continue;
                }
                return continuation.arrival;
            }

            (void)successor_route_segment;
            return std::nullopt;
        }

    }  // namespace

    bool is_branch_extension_feasible(
          const BranchState&       state
        , const ConnectionSegment& candidate
        , const RouteSegment&      candidate_route_segment
        , const TransferLimits&    limits
    ) noexcept {
        //tex:
        // Temporal suitability for a timed successor $$s^*_{x,y}$$ is checked
        // against the current partial connection $$c^*_x$$:
        // $$DEP(s^*_{x,y})-ARR(c^*_x)\in[MINTWT,MAXTWT].$$
        // Walk successors are always available, and the hard bound
        // $$NT(c^*_y)\le MAXNT$$ is enforced before a new timed transfer is accepted.
        // Same-line transfer is rejected except for the explicit repeated-stop
        // reboarding case on the same route pattern, where the candidate boards
        // a later occurrence of the same physical stop on a loop.
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

    std::optional<SearchBranchPhase> timed_extension_transition(
        SearchBranchPhase phase
    ) noexcept {
        switch (phase) {
            case SearchBranchPhase::BeforeFirstBoarding:
            case SearchBranchPhase::AfterTimedRide:
            case SearchBranchPhase::AfterTransferWalk:
                return SearchBranchPhase::AfterTimedRide;

            case SearchBranchPhase::AtOrigin:
            case SearchBranchPhase::Completed:
                return std::nullopt;
        }

        return std::nullopt;
    }

    bool first_timed_departure_allowed(
          const SearchBranch&      branch
        , const ConnectionSegment& successor
        , const SearchTimeDomain*  first_departure_domain
        , const TransferLimits&    limits
    ) noexcept {
        (void)limits;
        if (branch.metrics.departure.has_value()) {
            return true;
        }
        if (first_departure_domain == nullptr || !successor.departure.has_value()) {
            return true;
        }
        return contains(*first_departure_domain, *successor.departure);
    }

    bool is_same_line_transfer_candidate(
          const SearchBranch&      branch
        , const ConnectionSegment& successor
        , const RouteSegment&      successor_route_segment
    ) noexcept {
        if (!branch.trace.last_timed_segment || !branch.trace.last_timed_route_segment) {
            return false;
        }
        if (!is_timed_segment(successor)) {
            return false;
        }
        if (!same_line(*branch.trace.last_timed_route_segment, successor_route_segment)) {
            return false;
        }
        return successor.trip != branch.trace.last_timed_segment->trip;
    }

    bool is_repeated_stop_reboarding_case(
          const SearchBranch&      branch
        , const ConnectionSegment& successor
        , const RouteSegment&      successor_route_segment
    ) noexcept {
        if (!is_same_line_transfer_candidate(branch, successor, successor_route_segment)) {
            return false;
        }
        const auto* current_line = line_topology_of(*branch.trace.last_timed_route_segment);
        const auto* successor_line = line_topology_of(successor_route_segment);
        if (!current_line || !successor_line) {
            return false;
        }
        if (current_line->route != successor_line->route) {
            return false;
        }
        if (current_line->to.stop != successor_line->from.stop) {
            return false;
        }
        if (!branch.trace.last_timed_segment->to_index.has_value()
            || !successor.from_index.has_value()) {
            return false;
        }
        return branch.trace.last_timed_segment->to_index.value()
             < successor.from_index.value();
    }

    bool improves_repeated_stop_reboarding(
          const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const ConnectionSegment&   successor
        , const RouteSegment&        successor_route_segment
    ) noexcept {
        if (!is_repeated_stop_reboarding_case(branch, successor, successor_route_segment)) {
            return true;
        }
        const auto continuation_arrival = same_trip_continuation_arrival(
              branch
            , network
            , successor
            , successor_route_segment
        );
        if (!continuation_arrival.has_value() || !successor.arrival.has_value()) {
            return false;
        }
        return successor.arrival->value() < continuation_arrival->value();
    }

}  // namespace timetable::domain::assignment
