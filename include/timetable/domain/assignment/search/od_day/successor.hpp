#pragma once

#include <optional>

#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/od_day/supply_graph.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    struct DayLevelTimedSupportPropagation final {
        DayLevelTimedSupportLabel label;
        TimedSupportEnvelope      envelope{};
    };

    [[nodiscard]] std::optional<DayLevelTimedSupportLabel> make_day_level_timed_support_label(
        const ConnectionSegment& connection
    ) noexcept;

    [[nodiscard]] std::optional<DayLevelTimedSupportLabel> feasible_day_level_timed_support_label(
          const PreprocessedNetwork& network
        , const SearchBranch&        branch
        , ConnectionSegmentId        connection_id
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
    );

    [[nodiscard]] std::optional<DayLevelTimedSupportPropagation> propagate_day_level_timed_support(
          const PreprocessedNetwork& network
        , const SearchBranch&        branch
        , const DayLevelRideSupport& support
        , const TransferLimits&      limits
        , const SearchTimeDomain*    first_departure_domain
    );

    template <typename Visitor, typename RejectedWalkVisitor>
    void for_each_day_level_supply_successor(
          const DayLevelSupplySearchGraph& day_graph
        , const PreprocessedNetwork&       network
        , ZoneId                           origin
        , const ActiveDestinationMembership& active_destinations
        , const SearchBranch&              branch
        , const TransferLimits&            limits
        , const SearchTimeDomain*          first_departure_domain
        , Visitor&&                        visit
        , RejectedWalkVisitor&&            reject_walk
    ) {
        auto&& visitor = visit;
        auto&& walk_rejection_visitor = reject_walk;

        auto visit_walk_bucket = [&](const auto& buckets) {
            const auto found = buckets.find(branch.trace.current_physical);
            if (found != buckets.end()) {
                for (const auto& support : found->second) {
                    for (const auto connection_id : support.support_labels) {
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
                                    , .day_level_edge  = support.edge
                                }
                            );
                            break;
                        }
                    }
                }
            }
        };

        switch (branch.trace.phase) {
            case SearchBranchPhase::AtOrigin:
                visit_walk_bucket(day_graph.access_walks_by_from);
                break;

            case SearchBranchPhase::AfterTimedRide:
                visit_walk_bucket(day_graph.egress_walks_by_from);
                visit_walk_bucket(day_graph.transfer_walks_by_from);
                break;

            case SearchBranchPhase::BeforeFirstBoarding:
            case SearchBranchPhase::AfterTransferWalk: {
                const auto found = day_graph.transfer_walks_by_from.find(
                    branch.trace.current_physical
                );
                walk_rejection_visitor(
                    found == day_graph.transfer_walks_by_from.end()
                        ? 0u
                        : found->second.size()
                );
                break;
            }

            case SearchBranchPhase::Completed:
                break;
        }

        const auto ride_bucket = day_graph.rides_by_from.find(
            branch.trace.current_physical
        );
        if (ride_bucket == day_graph.rides_by_from.end()) {
            return;
        }

        for (const auto& support : ride_bucket->second) {
            const auto propagation = propagate_day_level_timed_support(
                  network
                , branch
                , support
                , limits
                , first_departure_domain
            );
            if (propagation.has_value()) {
                visitor(
                    SearchSuccessor{
                          .connection       = propagation->label.connection
                        , .day_level_edge   = support.edge
                        , .support_envelope = propagation->envelope
                    }
                );
            }
        }
    }

}  // namespace timetable::domain::assignment
