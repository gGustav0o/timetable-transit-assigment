#pragma once

#include <optional>

#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    struct BranchState final {
        std::optional<Time>          current_arrival_time{};
        const ConnectionSegment*     last_segment{};
        const RouteSegment*          last_route_segment{};
        std::optional<TransferCount> transfer_count{};
    };

    /**
     * @brief Search-level feasibility predicate for extending a connection branch.
     *
     * Enforces temporal suitability and forbids transfers to the same TRIP_ID.
     * Transfers within the same line are forbidden by default and are only
     * allowed in the explicit repeated-stop reboarding case from the assignment
     * algorithm. The candidate is interpreted relative to the full current
     * branch state, not only to one predecessor segment.
     */
    bool is_branch_extension_feasible(
          const BranchState&         state
        , const ConnectionSegment& candidate
        , const RouteSegment&      candidate_route_segment
        , const TransferLimits&    limits
    ) noexcept;

    [[nodiscard]] std::optional<SearchBranchPhase> timed_extension_transition(
        SearchBranchPhase phase
    ) noexcept;

    [[nodiscard]] bool first_timed_departure_allowed(
          const SearchBranch&      branch
        , const ConnectionSegment& successor
        , const SearchTimeDomain*  first_departure_domain
        , const TransferLimits&    limits
    ) noexcept;

    [[nodiscard]] bool is_same_line_transfer_candidate(
          const SearchBranch&      branch
        , const ConnectionSegment& successor
        , const RouteSegment&      successor_route_segment
    ) noexcept;

    [[nodiscard]] bool is_repeated_stop_reboarding_case(
          const SearchBranch&      branch
        , const ConnectionSegment& successor
        , const RouteSegment&      successor_route_segment
    ) noexcept;

    [[nodiscard]] bool improves_repeated_stop_reboarding(
          const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const ConnectionSegment&   successor
        , const RouteSegment&        successor_route_segment
    ) noexcept;

}  // namespace timetable::domain::assignment
