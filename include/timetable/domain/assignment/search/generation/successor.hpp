#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_set>

#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/od_day_path_search.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segments.hpp"

namespace timetable::domain::assignment {

    struct WalkExtensionTransition final {
        ConnectionLegKind kind{ ConnectionLegKind::AccessWalk };
        SearchBranchPhase next_phase{ SearchBranchPhase::BeforeFirstBoarding };
    };

    [[nodiscard]] std::optional<WalkExtensionTransition> walk_extension_transition_table(
          SearchBranchPhase phase
        , EndpointKey        from
        , EndpointKey        to
        , bool               active_destination
    ) noexcept;

    struct ActiveDestinationMembership final {
        const ActiveIndexSet*                       active_targets{};
        std::span<const SearchCompletionTarget>     batch_targets{};
        const std::unordered_set<std::int64_t>*     direct_destination_ids{};
    };

    [[nodiscard]] bool is_active_batch_destination(
          EndpointKey                        endpoint
        , const ActiveDestinationMembership& membership
    ) noexcept;

    [[nodiscard]] bool can_start_transfer_walk(
          const SearchBranch&   branch
        , const TransferLimits& limits
    ) noexcept;

    [[nodiscard]] std::optional<WalkExtensionTransition> admissible_walk_extension_transition(
          ZoneId                              origin
        , const ActiveDestinationMembership&  active_destinations
        , const SearchBranch&                 branch
        , const RouteSegment&                 route_segment
    ) noexcept;

    [[nodiscard]] std::span<const ConnectionSegmentId> access_walk_connections_from(
          const preprocessing::ConnectionSegmentIndex& index
        , EndpointKey                                  from
    ) noexcept;

    [[nodiscard]] std::span<const ConnectionSegmentId> transfer_walk_connections_from(
          const preprocessing::ConnectionSegmentIndex& index
        , EndpointKey                                  from
    ) noexcept;

    [[nodiscard]] std::span<const ConnectionSegmentId> egress_walk_connections_from(
          const preprocessing::ConnectionSegmentIndex& index
        , EndpointKey                                  from
    ) noexcept;

    [[nodiscard]] TimedSupportEnvelope propagate_timed_support_envelope(
          const ConnectionSegment& connection
        , const RouteSegment&      route_segment
    );

    [[nodiscard]] bool better_timed_support_label(
          const TimedSupportLabel& lhs
        , const TimedSupportLabel& rhs
    ) noexcept;

    void retain_timed_support_label(
          TimedSupportLabelVector& labels
        , TimedSupportLabel        label
    );

    [[nodiscard]] TimedSupportEnvelope make_timed_support_envelope(
          const RouteSegment&     route_segment
        , TimedSupportLabelVector labels
    );

    struct SearchSuccessor final {
        ConnectionSegmentId                    connection;
        std::optional<WalkExtensionTransition> walk_transition{};
        std::optional<DayLevelSupplyEdgeRef>   day_level_edge{};
        std::optional<TimedSupportEnvelope>    support_envelope{};
    };

    enum class PaperConnectionPrefixKind : std::uint8_t {
          UntimedAccessPrefix
        , ConnectionPrefix
    };

    struct PaperConnectionCandidateMetrics final {
        PaperConnectionNodeKey node{};
        SearchPruningMetrics   metrics{};
    };

    struct PaperConnectionPrefixEvaluation final {
        PaperConnectionPrefixKind kind{ PaperConnectionPrefixKind::UntimedAccessPrefix };
        std::optional<PaperConnectionCandidateMetrics> connection_candidate{};
    };

}  // namespace timetable::domain::assignment
