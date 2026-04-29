#pragma once

#include <cstdint>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Phase of a branch in the timetable connection tree.
     *
     * The phase is a derived property of a concrete search branch, not mutable
     * state. It captures the structural constraints of the paper's connection
     * tree: access before the first timed ride, then ride/transfer/egress after
     * a timed ride, and completion only at the task destination zone.
     */
    enum class SearchBranchPhase : std::uint8_t {
          BeforeFirstBoarding
        , AfterTimedRide
        , Completed
    };

    /**
     * @brief State used by timetable-independent suffix reachability.
     *
     * This is an intentionally relaxed model of the remaining search problem.
     * It ignores concrete departures, arrivals and waiting times. Therefore, if
     * a suffix is impossible in this model, it is also impossible in the full
     * timetable search. The converse is not required.
     */
    struct RelaxedSuffixState final {
        EndpointKey       current_physical{};
        ZoneId            destination{};
        SearchBranchPhase phase{ SearchBranchPhase::BeforeFirstBoarding };
        TransferCount     remaining_transfers{};
    };

    [[nodiscard]] constexpr bool has_timed_ride(
        SearchBranchPhase phase
    ) noexcept {
        return phase == SearchBranchPhase::AfterTimedRide
            || phase == SearchBranchPhase::Completed;
    }

    [[nodiscard]] constexpr bool is_completed(
        SearchBranchPhase phase
    ) noexcept {
        return phase == SearchBranchPhase::Completed;
    }

}  // namespace timetable::domain::assignment
