#pragma once

#include <optional>

#include "timetable/domain/params.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    struct BranchState final {
        std::optional<Time>             start_time{};
        std::optional<Time>             current_arrival_time{};
        const ConnectionSegment*        last_segment{};
        std::optional<TransferCount>    transfer_count{};
    };

    /**
     * @brief Search-level feasibility predicate for extending a connection branch.
     *
     * Enforces temporal suitability, start-wait policy, and forbids transfers
     * to the same TRIP_ID. The candidate is interpreted relative to the full
     * current branch state, not only to one predecessor segment.
     */
    bool is_branch_extension_feasible(
        const BranchState& state
        , const ConnectionSegment& candidate
        , const TransferLimits& limits
    ) noexcept;

}  // namespace timetable::domain::assignment
