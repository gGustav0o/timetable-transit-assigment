#include "timetable/domain/assignment/search/od_day/successor.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

#include "timetable/domain/assignment/search/branch_state.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
    namespace {

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

        std::optional<Time> same_trip_continuation_arrival_from_label(
              const PreprocessedNetwork&       network
            , const DayLevelTimedSupportLabel& label
            , const ConnectionSegment&         successor
            , const RouteSegment&              successor_route_segment
        ) noexcept {
            if (
                   !label.trip.has_value()
                || !label.to_index.has_value()
                || !successor.to_index.has_value()
            ) {
                return std::nullopt;
            }

            const auto& current_route_segment = route_segment_at(network, label.route_segment);
            const auto* current_line          = line_topology_of(current_route_segment);
            const auto* successor_line        = line_topology_of(successor_route_segment);
            if (!current_line || !successor_line) {
                return std::nullopt;
            }
            if (current_line->line != successor_line->line
                || current_line->route != successor_line->route) {
                return std::nullopt;
            }

            const auto current_stop           = occurrence_key(current_line->to);
            const auto desired_route_to_index = successor.to_index.value();
            const auto range                  = timed_bucket_range(network.connection_index, current_stop);
            if (!range) {
                return std::nullopt;
            }

            const auto [start, end] = *range;
            for (std::size_t i = start; i < end; ++i) {
                const auto  connection_id              = network.connection_index.timed_order[i];
                const auto& continuation               = connection_segment_at(network, connection_id);
                const auto& continuation_route_segment = route_segment_at(
                      network
                    , continuation.route_segment
                );
                const auto* continuation_line = line_topology_of(continuation_route_segment);
                if (!continuation_line) {
                    continue;
                }
                if (continuation_line->line != current_line->line
                    || continuation_line->route != current_line->route) {
                    continue;
                }
                if (continuation.trip != label.trip) {
                    continue;
                }
                if (continuation.from_index != label.to_index) {
                    continue;
                }
                if (continuation.to_index != desired_route_to_index) {
                    continue;
                }
                return continuation.arrival;
            }

            return std::nullopt;
        }

        bool improves_repeated_stop_reboarding_from_label(
              const PreprocessedNetwork&       network
            , const SearchBranch&              branch
            , const DayLevelTimedSupportLabel& label
            , const ConnectionSegment&         successor
            , const RouteSegment&              successor_route_segment
        ) noexcept {
            if (!branch.metrics.departure.has_value()) {
                return true;
            }
            if (!successor.departure.has_value()) {
                return true;
            }

            const auto& current_route_segment = route_segment_at(network, label.route_segment);
            const auto* current_line          = line_topology_of(current_route_segment);
            const auto* successor_line        = line_topology_of(successor_route_segment);
            if (!current_line || !successor_line) {
                return true;
            }
            if (current_line->line != successor_line->line
                || current_line->route != successor_line->route) {
                return true;
            }
            if (!label.trip.has_value() || !successor.trip.has_value()
                || label.trip == successor.trip) {
                return true;
            }
            if (current_line->to.stop != successor_line->from.stop) {
                return true;
            }
            if (!label.to_index.has_value() || !successor.from_index.has_value()) {
                return true;
            }
            if (!(label.to_index.value() < successor.from_index.value())) {
                return true;
            }

            const auto continuation_arrival = same_trip_continuation_arrival_from_label(
                  network
                , label
                , successor
                , successor_route_segment
            );
            if (!continuation_arrival.has_value() || !successor.arrival.has_value()) {
                return false;
            }
            return successor.arrival->value() < continuation_arrival->value();
        }

        [[nodiscard]] BranchState day_level_support_branch_state(
            const SearchBranch& branch
        ) noexcept {
            return BranchState{
                  .current_arrival_time = branch.metrics.current_time
                , .last_segment         = branch.trace.last_timed_segment
                , .last_route_segment   = branch.trace.last_timed_route_segment
                , .transfer_count       = branch.metrics.departure.has_value()
                    ? std::optional<TransferCount>{ branch.metrics.transfers }
                    : std::nullopt
            };
        }

        [[nodiscard]] std::optional<Time> support_label_current_time(
              const SearchBranch&              branch
            , const DayLevelTimedSupportLabel& label
        ) noexcept {
            if (!branch.metrics.current_time.has_value()) {
                return std::nullopt;
            }
            if (branch.trace.last_timed_segment == nullptr
                || !branch.trace.last_timed_segment->arrival.has_value()) {
                return label.arrival;
            }

            return Time{
                label.arrival.value()
                + branch.metrics.current_time->value()
                - branch.trace.last_timed_segment->arrival->value()
            };
        }

        [[nodiscard]] BranchState day_level_support_label_branch_state(
              const PreprocessedNetwork&       network
            , const SearchBranch&              branch
            , const DayLevelTimedSupportLabel& label
        ) noexcept {
            return BranchState{
                  .current_arrival_time = support_label_current_time(branch, label)
                , .last_segment         = &connection_segment_at(network, label.connection)
                , .last_route_segment   = &route_segment_at(network, label.route_segment)
                , .transfer_count       = branch.metrics.departure.has_value()
                    ? std::optional<TransferCount>{ branch.metrics.transfers }
                    : std::nullopt
            };
        }

    }  // namespace

    std::optional<DayLevelTimedSupportLabel> make_day_level_timed_support_label(
        const ConnectionSegment& connection
    ) noexcept {
        if (!connection.departure.has_value() || !connection.arrival.has_value()) {
            return std::nullopt;
        }

        return DayLevelTimedSupportLabel{
              .connection    = connection.id
            , .route_segment = connection.route_segment
            , .trip          = connection.trip
            , .from_index    = connection.from_index
            , .to_index      = connection.to_index
            , .departure     = *connection.departure
            , .arrival       = *connection.arrival
        };
    }

    std::optional<DayLevelTimedSupportLabel> feasible_day_level_timed_support_label(
          const PreprocessedNetwork&  network
        , const SearchBranch&         branch
        , ConnectionSegmentId         connection_id
        , const TransferLimits&       limits
        , const SearchTimeDomain*     first_departure_domain
    ) {
        const auto state = day_level_support_branch_state(branch);
        const auto& connection = connection_segment_at(network, connection_id);
        const auto& route_segment = route_segment_at(network, connection.route_segment);
        const auto label = make_day_level_timed_support_label(connection);
        if (!label.has_value()) {
            return std::nullopt;
        }
        if (!first_timed_departure_allowed(
                  branch
                , connection
                , first_departure_domain
                , limits
        )) {
            return std::nullopt;
        }

        if (!branch.od_day_carrier.support_envelope.labels.empty()
            && branch.metrics.departure.has_value()) {
            const auto feasible_from_envelope = std::any_of(
                  branch.od_day_carrier.support_envelope.labels.begin()
                , branch.od_day_carrier.support_envelope.labels.end()
                , [&](const DayLevelTimedSupportLabel& support_label) {
                      return is_branch_extension_feasible(
                            day_level_support_label_branch_state(
                                  network
                                , branch
                                , support_label
                            )
                          , connection
                          , route_segment
                          , limits
                      )
                      && improves_repeated_stop_reboarding_from_label(
                            network
                          , branch
                          , support_label
                          , connection
                          , route_segment
                      );
                  }
            );
            if (!feasible_from_envelope) {
                return std::nullopt;
            }
        } else {
            if (!is_branch_extension_feasible(
                      state
                    , connection
                    , route_segment
                    , limits
            )) {
                return std::nullopt;
            }
            if (!improves_repeated_stop_reboarding(
                      branch
                    , network
                    , connection
                    , route_segment
            )) {
                return std::nullopt;
            }
        }
        return label;
    }

    std::optional<DayLevelTimedSupportPropagation> propagate_day_level_timed_support(
          const PreprocessedNetwork& network
        , const SearchBranch&        branch
        , const DayLevelRideSupport& support
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
    ) {
        TimedSupportLabelVector feasible_labels;
        feasible_labels.reserve(support.support_labels.size());
        std::optional<TimedSupportLabel> representative;
        std::optional<RouteSegmentId> representative_route_segment;
        for (const auto connection_id : support.support_labels) {
            const auto label = feasible_day_level_timed_support_label(
                  network
                , branch
                , connection_id
                , limits
                , first_departure_domain
            );
            if (label.has_value()) {
                if (!representative.has_value()
                    || better_timed_support_label(*label, *representative)) {
                    representative = *label;
                    representative_route_segment = label->route_segment;
                }
                retain_timed_support_label(feasible_labels, *label);
            }
        }
        if (!representative.has_value() || !representative_route_segment.has_value()) {
            return std::nullopt;
        }
        return DayLevelTimedSupportPropagation{
              .label = *representative
            , .envelope = make_timed_support_envelope(
                  route_segment_at(network, *representative_route_segment)
                , std::move(feasible_labels)
              )
        };
    }

}  // namespace timetable::domain::assignment
