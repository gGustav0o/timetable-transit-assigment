#include "timetable/domain/assignment/search/generation/successor.hpp"

#include <algorithm>
#include <utility>

#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {

    std::optional<WalkExtensionTransition> walk_extension_transition_table(
          SearchBranchPhase phase
        , EndpointKey        from
        , EndpointKey        to
        , bool               active_destination
    ) noexcept {
        switch (phase) {
            case SearchBranchPhase::AtOrigin:
                if (from.kind == EndpointKind::Zone
                    && to.kind == EndpointKind::Stop) {
                    return WalkExtensionTransition{
                          .kind       = ConnectionLegKind::AccessWalk
                        , .next_phase = SearchBranchPhase::BeforeFirstBoarding
                    };
                }
                return std::nullopt;

            case SearchBranchPhase::AfterTimedRide:
                if (from.kind == EndpointKind::Stop
                    && to.kind == EndpointKind::Stop) {
                    return WalkExtensionTransition{
                          .kind       = ConnectionLegKind::TransferWalk
                        , .next_phase = SearchBranchPhase::AfterTransferWalk
                    };
                }
                if (from.kind == EndpointKind::Stop
                    && to.kind == EndpointKind::Zone
                    && active_destination) {
                    return WalkExtensionTransition{
                          .kind       = ConnectionLegKind::EgressWalk
                        , .next_phase = SearchBranchPhase::Completed
                    };
                }
                return std::nullopt;

            case SearchBranchPhase::BeforeFirstBoarding:
            case SearchBranchPhase::AfterTransferWalk:
            case SearchBranchPhase::Completed:
                return std::nullopt;
        }

        return std::nullopt;
    }

    bool is_active_batch_destination(
          EndpointKey                        endpoint
        , const ActiveDestinationMembership& membership
    ) noexcept {
        if (endpoint.kind != EndpointKind::Zone) {
            return false;
        }
        if (membership.direct_destination_ids != nullptr) {
            return membership.direct_destination_ids->find(endpoint.id)
                != membership.direct_destination_ids->end();
        }
        if (membership.active_targets == nullptr) {
            return false;
        }

        bool active = false;
        membership.active_targets->for_each_index([&](std::size_t target_pos) {
            if (membership.batch_targets[target_pos].destination.get() == endpoint.id) {
                active = true;
            }
        });
        return active;
    }

    bool can_start_transfer_walk(
          const SearchBranch&   branch
        , const TransferLimits& limits
    ) noexcept {
        if (!branch.metrics.departure.has_value()) {
            return true;
        }
        return branch.metrics.transfers.get() < limits.max_transfers.get();
    }

    std::optional<WalkExtensionTransition> admissible_walk_extension_transition(
          ZoneId                              origin
        , const ActiveDestinationMembership&  active_destinations
        , const SearchBranch&                 branch
        , const RouteSegment&                 route_segment
    ) noexcept {
        const auto from_physical = branch.trace.current_physical;
        const auto next_physical = physical_to_key(route_segment);

        if (branch.trace.phase == SearchBranchPhase::AtOrigin
            && (from_physical.kind != EndpointKind::Zone
                || from_physical.id != origin.get())) {
            return std::nullopt;
        }

        return walk_extension_transition_table(
              branch.trace.phase
            , from_physical
            , next_physical
            , is_active_batch_destination(
                  next_physical
                , active_destinations
              )
        );
    }

    template <class Key>
    [[nodiscard]] std::span<const ConnectionSegmentId> indexed_connection_span(
          std::span<const ConnectionSegmentId> order
        , std::span<const Key>                 buckets
        , std::span<const std::size_t>         offsets
        , const Key&                           key
    ) noexcept {
        const auto bucket = preprocessing::find_bucket(buckets, key);
        if (!bucket) {
            return {};
        }
        const auto i = *bucket;
        const auto start = offsets[i];
        const auto end = offsets[i + 1u];
        if (start >= end) {
            return {};
        }
        return std::span<const ConnectionSegmentId>{
              order.data() + start
            , end - start
        };
    }

    std::span<const ConnectionSegmentId> access_walk_connections_from(
          const preprocessing::ConnectionSegmentIndex& index
        , EndpointKey                                  from
    ) noexcept {
        return indexed_connection_span<EndpointKey>(
              std::span<const ConnectionSegmentId>{
                  index.access_walk_order.data()
                , index.access_walk_order.size()
              }
            , std::span<const EndpointKey>{
                  index.access_walk_buckets.data()
                , index.access_walk_buckets.size()
              }
            , std::span<const std::size_t>{
                  index.access_walk_offsets.data()
                , index.access_walk_offsets.size()
              }
            , from
        );
    }

    std::span<const ConnectionSegmentId> transfer_walk_connections_from(
          const preprocessing::ConnectionSegmentIndex& index
        , EndpointKey                                  from
    ) noexcept {
        return indexed_connection_span<EndpointKey>(
              std::span<const ConnectionSegmentId>{
                  index.transfer_walk_order.data()
                , index.transfer_walk_order.size()
              }
            , std::span<const EndpointKey>{
                  index.transfer_walk_buckets.data()
                , index.transfer_walk_buckets.size()
              }
            , std::span<const std::size_t>{
                  index.transfer_walk_offsets.data()
                , index.transfer_walk_offsets.size()
              }
            , from
        );
    }

    std::span<const ConnectionSegmentId> egress_walk_connections_from(
          const preprocessing::ConnectionSegmentIndex& index
        , EndpointKey                                  from
    ) noexcept {
        return indexed_connection_span<EndpointKey>(
              std::span<const ConnectionSegmentId>{
                  index.egress_walk_order.data()
                , index.egress_walk_order.size()
              }
            , std::span<const EndpointKey>{
                  index.egress_walk_buckets.data()
                , index.egress_walk_buckets.size()
              }
            , std::span<const std::size_t>{
                  index.egress_walk_offsets.data()
                , index.egress_walk_offsets.size()
              }
            , from
        );
    }

    TimedSuccessorEnumerationResult collect_timed_connection_successors(
          const PreprocessedNetwork& network
        , EndpointKey                physical_from
        , std::optional<Time>        current_time
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
    ) {
        TimedSuccessorEnumerationResult result;
        if (!current_time.has_value() && first_departure_domain != nullptr) {
            result.diagnostics.scanned_windows = first_departure_domain->windows.size();
        } else if (physical_from.kind == EndpointKind::Stop) {
            result.diagnostics.scanned_windows = 1u;
        }
        for_each_timed_connection_successor(
              network
            , physical_from
            , current_time
            , limits
            , first_departure_domain
            , [&](ConnectionSegmentId successor) {
                  result.successors.push_back(successor);
              }
        );
        result.diagnostics.emitted_successors = result.successors.size();
        return result;
    }

    TimedSupportEnvelope propagate_timed_support_envelope(
          const ConnectionSegment& connection
        , const RouteSegment&      route_segment
    ) {
        return TimedSupportEnvelope{
              .key = TimedSupportEnvelopeKey{
                  .last_timed_occurrence = occurrence_key(line_topology_of(route_segment)->to)
                , .last_line             = line_of(route_segment)
              }
            , .labels = TimedSupportLabelVector{
                  TimedSupportLabel{
                      .connection    = connection.id
                    , .route_segment = connection.route_segment
                    , .trip          = connection.trip
                    , .from_index    = connection.from_index
                    , .to_index      = connection.to_index
                    , .departure     = *connection.departure
                    , .arrival       = *connection.arrival
                  }
              }
        };
    }

    bool better_timed_support_label(
          const TimedSupportLabel& lhs
        , const TimedSupportLabel& rhs
    ) noexcept {
        if (lhs.arrival.value() != rhs.arrival.value()) {
            return lhs.arrival.value() < rhs.arrival.value();
        }
        if (lhs.departure.value() != rhs.departure.value()) {
            return lhs.departure.value() > rhs.departure.value();
        }
        return lhs.connection.get() < rhs.connection.get();
    }

    void retain_timed_support_label(
          TimedSupportLabelVector& labels
        , TimedSupportLabel        label
    ) {
        if (std::find(labels.begin(), labels.end(), label) != labels.end()) {
            return;
        }
        labels.push_back(std::move(label));
        std::sort(labels.begin(), labels.end(), better_timed_support_label);
    }

    TimedSupportEnvelope make_timed_support_envelope(
          const RouteSegment&     route_segment
        , TimedSupportLabelVector labels
    ) {
        std::sort(labels.begin(), labels.end(), better_timed_support_label);
        labels.erase(
              std::unique(labels.begin(), labels.end())
            , labels.end()
        );
        return TimedSupportEnvelope{
              .key = TimedSupportEnvelopeKey{
                  .last_timed_occurrence = occurrence_key(line_topology_of(route_segment)->to)
                , .last_line             = line_of(route_segment)
              }
            , .labels = std::move(labels)
        };
    }

}  // namespace timetable::domain::assignment
