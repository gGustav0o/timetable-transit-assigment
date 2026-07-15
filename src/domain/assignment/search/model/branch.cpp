#include "timetable/domain/assignment/search/model/branch.hpp"

#include <cstdint>
#include <string>

#include <mathfp/core/error.hpp>

#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment {

    const char* search_branch_phase_name(
        SearchBranchPhase phase
    ) noexcept {
        switch (phase) {
            case SearchBranchPhase::AtOrigin:
                return "at_origin";

            case SearchBranchPhase::BeforeFirstBoarding:
                return "before_first_boarding";

            case SearchBranchPhase::AfterTimedRide:
                return "after_timed_ride";

            case SearchBranchPhase::AfterTransferWalk:
                return "after_transfer_walk";

            case SearchBranchPhase::Completed:
                return "completed";
        }

        return "unknown";
    }

    namespace {

        [[nodiscard]] mathfp::Expected<mathfp::Unit> phase_invariant_error(
              const SearchBranch& branch
            , const char*         reason
        ) {
            return mathfp::unexpected(
                mathfp::internal_error("search branch phase invariant violation")
                    .ctx("reason", std::string(reason))
                    .ctx("phase", std::string(search_branch_phase_name(branch.trace.phase)))
                    .ctx("origin", branch.trace.origin.get())
                    .ctx("current_kind", static_cast<std::int64_t>(branch.trace.current_physical.kind))
                    .ctx("current_id", branch.trace.current_physical.id)
            );
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_search_branch_phase_invariants(
        const SearchBranch& branch
    ) {
        const auto physical = branch.trace.current_physical;
        switch (branch.trace.phase) {
            case SearchBranchPhase::AtOrigin:
                if (physical != endpoint_key(branch.trace.origin)) {
                    return phase_invariant_error(branch, "at-origin branch is not located at its origin zone");
                }
                if (branch.trace.current_occurrence.has_value()) {
                    return phase_invariant_error(branch, "at-origin branch has stop occurrence");
                }
                if (branch.trace.parent_branch.has_value()
                    || branch.trace.incoming_segment.has_value()) {
                    return phase_invariant_error(branch, "at-origin branch has predecessor edge");
                }
                if (branch.trace.last_timed_segment != nullptr
                    || branch.trace.last_timed_route_segment != nullptr) {
                    return phase_invariant_error(branch, "at-origin branch has timed context");
                }
                if (branch.metrics.departure.has_value()
                    || branch.metrics.current_time.has_value()) {
                    return phase_invariant_error(branch, "at-origin branch has time state");
                }
                return mathfp::kUnit;

            case SearchBranchPhase::BeforeFirstBoarding:
                if (physical.kind != EndpointKind::Stop) {
                    return phase_invariant_error(branch, "preboarding branch is not located at a stop");
                }
                if (branch.trace.current_occurrence.has_value()) {
                    return phase_invariant_error(branch, "preboarding branch has stop occurrence");
                }
                if (!branch.trace.parent_branch.has_value()
                    || !branch.trace.incoming_segment.has_value()) {
                    return phase_invariant_error(branch, "preboarding branch has no access-walk predecessor");
                }
                if (branch.trace.last_timed_segment != nullptr
                    || branch.trace.last_timed_route_segment != nullptr) {
                    return phase_invariant_error(branch, "preboarding branch has timed context");
                }
                if (branch.metrics.departure.has_value()
                    || branch.metrics.current_time.has_value()) {
                    return phase_invariant_error(branch, "preboarding branch has time state");
                }
                return mathfp::kUnit;

            case SearchBranchPhase::AfterTimedRide:
                if (physical.kind != EndpointKind::Stop) {
                    return phase_invariant_error(branch, "after-timed branch is not located at a stop");
                }
                if (!branch.trace.current_occurrence.has_value()) {
                    return phase_invariant_error(branch, "after-timed branch has no stop occurrence");
                }
                if (branch.trace.last_timed_segment == nullptr
                    || branch.trace.last_timed_route_segment == nullptr) {
                    return phase_invariant_error(branch, "after-timed branch has no timed context");
                }
                if (!branch.metrics.departure.has_value()
                    || !branch.metrics.current_time.has_value()) {
                    return phase_invariant_error(branch, "after-timed branch has incomplete time state");
                }
                return mathfp::kUnit;

            case SearchBranchPhase::AfterTransferWalk:
                if (physical.kind != EndpointKind::Stop) {
                    return phase_invariant_error(branch, "after-transfer-walk branch is not located at a stop");
                }
                if (branch.trace.current_occurrence.has_value()) {
                    return phase_invariant_error(branch, "after-transfer-walk branch has stop occurrence");
                }
                if (branch.trace.last_timed_segment == nullptr
                    || branch.trace.last_timed_route_segment == nullptr) {
                    return phase_invariant_error(branch, "after-transfer-walk branch has no timed context");
                }
                if (!branch.metrics.departure.has_value()
                    || !branch.metrics.current_time.has_value()) {
                    return phase_invariant_error(branch, "after-transfer-walk branch has incomplete time state");
                }
                return mathfp::kUnit;

            case SearchBranchPhase::Completed:
                if (physical.kind != EndpointKind::Zone) {
                    return phase_invariant_error(branch, "completed branch is not located at a zone");
                }
                if (branch.trace.current_occurrence.has_value()) {
                    return phase_invariant_error(branch, "completed branch has stop occurrence");
                }
                if (branch.trace.last_timed_segment == nullptr
                    || branch.trace.last_timed_route_segment == nullptr) {
                    return phase_invariant_error(branch, "completed branch has no timed context");
                }
                if (!branch.metrics.departure.has_value()
                    || !branch.metrics.current_time.has_value()) {
                    return phase_invariant_error(branch, "completed branch has incomplete time state");
                }
                return mathfp::kUnit;
        }

        return phase_invariant_error(branch, "unknown branch phase");
    }

}  // namespace timetable::domain::assignment
