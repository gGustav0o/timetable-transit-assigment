#pragma once

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <span>
#include <unordered_set>
#include <vector>

#include "timetable/domain/assignment/search/branch_state.hpp"
#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/od_day_path_contract.hpp"
#include "timetable/domain/assignment/search/problem.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"
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

    struct TimedSuccessorEnumerationDiagnostics final {
        std::size_t scanned_windows{};
        std::size_t emitted_successors{};
    };

    struct TimedSuccessorEnumerationResult final {
        std::vector<ConnectionSegmentId>        successors{};
        TimedSuccessorEnumerationDiagnostics    diagnostics{};
    };

    template <typename Visitor>
    void for_each_boarding_successor_in_window(
          const PreprocessedNetwork& network
        , std::size_t                start
        , std::size_t                end
        , SearchTimeWindow           window
        , Visitor&&                  visit
    ) {
        auto&& visitor = visit;
        const auto departures_begin =
            network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(start);
        const auto departures_end =
            network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(end);
        const auto begin = std::lower_bound(
              departures_begin
            , departures_end
            , window.begin
            , [](const Time& lhs, const Time& rhs) {
                return lhs.value() < rhs.value();
            }
        );

        auto idx = start + static_cast<std::size_t>(begin - departures_begin);
        for (auto it = begin; it != departures_end; ++it, ++idx) {
            if (it->value() > window.end.value()) {
                break;
            }
            visitor(network.connection_index.boarding_order[idx]);
        }
    }

    template <typename Visitor>
    void for_each_timed_connection_successor(
          const PreprocessedNetwork& network
        , EndpointKey                physical_from
        , std::optional<Time>        current_time
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
        , Visitor&&                  visit
    ) {
        auto&& visitor = visit;
        if (physical_from.kind != EndpointKind::Stop) {
            return;
        }

        const auto bucket = preprocessing::find_bucket(
              network.connection_index.boarding_stop_buckets
            , StopId{ physical_from.id }
        );
        if (!bucket) {
            return;
        }

        const auto bucket_index = *bucket;
        const auto start = network.connection_index.boarding_offsets[bucket_index];
        const auto end = network.connection_index.boarding_offsets[bucket_index + 1];
        if (start >= end) {
            return;
        }

        if (!current_time.has_value()) {
            if (first_departure_domain != nullptr) {
                for (const auto& window : first_departure_domain->windows) {
                    for_each_boarding_successor_in_window(
                          network
                        , start
                        , end
                        , window
                        , visitor
                    );
                }
                return;
            }
            for (std::size_t i = start; i < end; ++i) {
                visitor(network.connection_index.boarding_order[i]);
            }
            return;
        }

        const auto earliest = Time{
            current_time->value() + limits.min_transfer_wait.value()
        };
        const auto latest = Time{
            current_time->value() + limits.max_transfer_wait.value()
        };
        const auto departures_begin =
            network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(start);
        const auto departures_end =
            network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(end);
        const auto begin = std::lower_bound(
              departures_begin
            , departures_end
            , earliest
            , [](const Time& lhs, const Time& rhs) {
                return lhs.value() < rhs.value();
            }
        );

        auto idx = start + static_cast<std::size_t>(begin - departures_begin);
        for (auto it = begin; it != departures_end; ++it, ++idx) {
            if (it->value() > latest.value()) {
                break;
            }
            visitor(network.connection_index.boarding_order[idx]);
        }
    }

    [[nodiscard]] TimedSuccessorEnumerationResult collect_timed_connection_successors(
          const PreprocessedNetwork& network
        , EndpointKey                physical_from
        , std::optional<Time>        current_time
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
    );

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

    struct PaperWalkLookupDiagnostics final {
        std::size_t access{};
        std::size_t transfer{};
        std::size_t egress{};
        std::size_t skipped_by_phase{};
        std::size_t skipped_by_transfer_budget{};
    };

    struct PaperSuccessorGenerationDiagnostics final {
        PaperWalkLookupDiagnostics walk_lookup{};
        std::size_t timed_lookup_skipped_phase{};
        std::size_t timed_lookup_skipped_transfer_budget{};
        std::size_t timed_successor_rejected_time_domain{};
        std::size_t timed_successor_rejected_same_trip{};
        std::size_t timed_successor_rejected_same_line{};
        std::size_t timed_successor_rejected_feasibility{};
    };

    enum class PaperTimedInsertabilityRejection : std::uint8_t {
          None
        , FirstDepartureDomain
        , SameTrip
        , SameLine
        , Feasibility
    };

    [[nodiscard]] inline bool paper_timed_lookup_allowed(
          const SearchBranch&              branch
        , const TransferLimits&            limits
        , PaperSuccessorGenerationDiagnostics* diagnostics
    ) {
        if (!timed_extension_transition(branch.trace.phase).has_value()) {
            if (diagnostics != nullptr) {
                ++diagnostics->timed_lookup_skipped_phase;
            }
            return false;
        }
        if (!branch.metrics.departure.has_value()) {
            return true;
        }
        if (branch.metrics.transfers >= limits.max_transfers) {
            if (diagnostics != nullptr) {
                ++diagnostics->timed_lookup_skipped_transfer_budget;
            }
            return false;
        }
        return true;
    }

    [[nodiscard]] inline PaperTimedInsertabilityRejection paper_timed_insertability_rejection(
          const PreprocessedNetwork& network
        , const SearchBranch&        branch
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
        , ConnectionSegmentId        connection_id
        , std::optional<Time>        current_arrival_time
    ) {
        const auto& successor = connection_segment_at(network, connection_id);
        const auto& route_segment = route_segment_at(network, successor.route_segment);
        if (!first_timed_departure_allowed(
              branch
            , successor
            , first_departure_domain
            , limits
        )) {
            return PaperTimedInsertabilityRejection::FirstDepartureDomain;
        }
        if (branch.trace.last_timed_segment != nullptr
            && transfer_reuses_same_trip(*branch.trace.last_timed_segment, successor)) {
            return PaperTimedInsertabilityRejection::SameTrip;
        }
        if (is_same_line_transfer_candidate(branch, successor, route_segment)) {
            const auto allowed_loop_reboarding =
                   is_repeated_stop_reboarding_case(branch, successor, route_segment)
                && improves_repeated_stop_reboarding(
                      branch
                    , network
                    , successor
                    , route_segment
                );
            if (!allowed_loop_reboarding) {
                return PaperTimedInsertabilityRejection::SameLine;
            }
        }
        const BranchState state{
              .current_arrival_time = current_arrival_time
            , .last_segment         = branch.trace.last_timed_segment
            , .last_route_segment   = branch.trace.last_timed_route_segment
            , .transfer_count       = branch.metrics.departure.has_value()
                ? std::optional<TransferCount>{ branch.metrics.transfers }
                : std::nullopt
        };
        if (!is_branch_extension_feasible(
              state
            , successor
            , route_segment
            , limits
        )) {
            return PaperTimedInsertabilityRejection::Feasibility;
        }
        return PaperTimedInsertabilityRejection::None;
    }

    [[nodiscard]] inline bool paper_timed_successor_insertable_before_visitor(
          const PreprocessedNetwork& network
        , const SearchBranch&        branch
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
        , ConnectionSegmentId        connection_id
        , std::optional<Time>        current_arrival_time
        , PaperSuccessorGenerationDiagnostics* diagnostics
    ) {
        const auto rejection = paper_timed_insertability_rejection(
              network
            , branch
            , limits
            , first_departure_domain
            , connection_id
            , current_arrival_time
        );
        switch (rejection) {
            case PaperTimedInsertabilityRejection::None:
                return true;

            case PaperTimedInsertabilityRejection::FirstDepartureDomain:
                if (diagnostics != nullptr) {
                    ++diagnostics->timed_successor_rejected_time_domain;
                }
                return false;

            case PaperTimedInsertabilityRejection::SameTrip:
                if (diagnostics != nullptr) {
                    ++diagnostics->timed_successor_rejected_same_trip;
                }
                return false;

            case PaperTimedInsertabilityRejection::SameLine:
                if (diagnostics != nullptr) {
                    ++diagnostics->timed_successor_rejected_same_line;
                }
                return false;

            case PaperTimedInsertabilityRejection::Feasibility:
                if (diagnostics != nullptr) {
                    ++diagnostics->timed_successor_rejected_feasibility;
                }
                return false;
        }
        return true;
    }

    template <typename Visitor>
    void for_each_paper_walk_successor(
          const PreprocessedNetwork&       network
        , ZoneId                           origin
        , const ActiveDestinationMembership& active_destinations
        , const SearchBranch&              branch
        , std::span<const ConnectionSegmentId> connections
        , Visitor&&                        visit
    ) {
        auto&& visitor = visit;
        for (const auto connection_id : connections) {
            const auto& connection = connection_segment_at(network, connection_id);
            const auto& route_segment = route_segment_at(network, connection.route_segment);
            if (auto transition = admissible_walk_extension_transition(
                  origin
                , active_destinations
                , branch
                , route_segment
            )) {
                visitor(
                    SearchSuccessor{
                          .connection      = connection_id
                        , .walk_transition = *transition
                    }
                );
            }
        }
    }

    template <typename Visitor>
    void for_each_paper_timed_ride_successor(
          const PreprocessedNetwork&       network
        , const SearchBranch&              branch
        , const TransferLimits&            limits
        , const SearchTimeDomain*          first_departure_domain
        , PaperSuccessorGenerationDiagnostics* diagnostics
        , Visitor&&                        visit
    ) {
        if (!paper_timed_lookup_allowed(branch, limits, diagnostics)) {
            return;
        }

        auto&& visitor = visit;
        auto visit_timed = [&](ConnectionSegmentId connection_id) {
            if (!paper_timed_successor_insertable_before_visitor(
                  network
                , branch
                , limits
                , first_departure_domain
                , connection_id
                , branch.metrics.current_time
                , diagnostics
            )) {
                return;
            }
            const auto& connection = connection_segment_at(network, connection_id);
            const auto& route_segment = route_segment_at(network, connection.route_segment);
            visitor(
                SearchSuccessor{
                      .connection = connection_id
                    , .support_envelope =
                        propagate_timed_support_envelope(connection, route_segment)
                }
            );
        };
        for_each_timed_connection_successor(
              network
            , branch.trace.current_physical
            , branch.metrics.current_time
            , limits
            , first_departure_domain
            , visit_timed
        );
    }

    template <typename Visitor>
    void for_each_paper_successor(
          const PreprocessedNetwork&       network
        , ZoneId                           origin
        , const ActiveDestinationMembership& active_destinations
        , const SearchBranch&              branch
        , const TransferLimits&            limits
        , const SearchTimeDomain*          first_departure_domain
        , PaperSuccessorGenerationDiagnostics* diagnostics
        , Visitor&&                        visit
    ) {
        auto&& visitor = visit;

        switch (branch.trace.phase) {
            case SearchBranchPhase::AtOrigin:
                if (diagnostics != nullptr) {
                    ++diagnostics->walk_lookup.access;
                }
                for_each_paper_walk_successor(
                      network
                    , origin
                    , active_destinations
                    , branch
                    , access_walk_connections_from(
                          network.connection_index
                        , branch.trace.current_physical
                    )
                    , visitor
                );
                break;

            case SearchBranchPhase::AfterTimedRide:
                if (diagnostics != nullptr) {
                    ++diagnostics->walk_lookup.egress;
                }
                for_each_paper_walk_successor(
                      network
                    , origin
                    , active_destinations
                    , branch
                    , egress_walk_connections_from(
                          network.connection_index
                        , branch.trace.current_physical
                    )
                    , visitor
                );
                if (can_start_transfer_walk(branch, limits)) {
                    if (diagnostics != nullptr) {
                        ++diagnostics->walk_lookup.transfer;
                    }
                    for_each_paper_walk_successor(
                          network
                        , origin
                        , active_destinations
                        , branch
                        , transfer_walk_connections_from(
                              network.connection_index
                            , branch.trace.current_physical
                        )
                        , visitor
                    );
                } else if (diagnostics != nullptr) {
                    ++diagnostics->walk_lookup.skipped_by_transfer_budget;
                }
                break;

            case SearchBranchPhase::BeforeFirstBoarding:
            case SearchBranchPhase::AfterTransferWalk:
                if (diagnostics != nullptr) {
                    ++diagnostics->walk_lookup.skipped_by_phase;
                }
                break;

            case SearchBranchPhase::Completed:
                break;
        }

        for_each_paper_timed_ride_successor(
              network
            , branch
            , limits
            , first_departure_domain
            , diagnostics
            , visitor
        );
    }

}  // namespace timetable::domain::assignment
