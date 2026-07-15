#include "timetable/domain/assignment/search/generation/branch_transition.hpp"

#include <cstdint>
#include <optional>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search/branch_state.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"
#include "timetable/domain/params/transfer_limits.hpp"
#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] ConnectionLeg make_transition_ride_leg(
              const ConnectionSegment& segment
            , const RouteSegment&      route_segment
        ) {
            const auto* line = line_topology_of(route_segment);
            return ConnectionLeg{
                  .kind               = ConnectionLegKind::Ride
                , .connection_segment = segment.id
                , .route_segment      = route_segment.id
                , .physical_from      = physical_from_key(route_segment)
                , .physical_to        = physical_to_key(route_segment)
                , .occurrence_from    = occurrence_key(line->from)
                , .occurrence_to      = occurrence_key(line->to)
                , .line               = line->line
                , .route              = line->route
                , .trip               = segment.trip
                , .start_time         = *segment.departure
                , .end_time           = *segment.arrival
                , .length             = route_segment.length
                , .fare               = segment.fare.value_or(0.0)
            };
        }

        [[nodiscard]] SearchPartialTrace extend_trace_with_walk(
              SearchPartialTrace            trace
            , std::size_t                    branch_index
            , const RouteSegment&            route_segment
            , ConnectionSegmentId            segment_id
            , std::optional<DayLevelSupplyEdgeRef> day_level_edge
            , const WalkExtensionTransition& transition
        ) {
            trace.parent_branch           = branch_index;
            trace.incoming_segment        = segment_id;
            trace.incoming_day_level_edge = day_level_edge;
            trace.current_physical        = physical_to_key(route_segment);
            trace.current_occurrence      = std::nullopt;
            trace.phase                   = transition.next_phase;
            return trace;
        }

        [[nodiscard]] SearchPartialTrace extend_trace_with_timed(
              SearchPartialTrace       trace
            , std::size_t              branch_index
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
            , SearchBranchPhase        next_phase
            , std::optional<DayLevelSupplyEdgeRef> day_level_edge
        ) {
            trace.parent_branch            = branch_index;
            trace.incoming_segment         = segment.id;
            trace.incoming_day_level_edge  = day_level_edge;
            trace.last_day_level_ride_edge = day_level_edge;
            trace.current_physical         = physical_to_key(route_segment);
            trace.current_occurrence       = occurrence_key(line_topology_of(route_segment)->to);
            trace.phase                    = next_phase;
            trace.last_timed_segment       = &segment;
            trace.last_timed_route_segment = &route_segment;
            return trace;
        }

        BranchState feasibility_state(
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

    }  // namespace

    bool branch_revisits_physical(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , EndpointKey                next
    ) {
        if (branch.trace.current_physical == next) {
            return true;
        }

        if (branch.od_day_carrier.support_prefix != nullptr) {
            for (auto node = branch.od_day_carrier.support_prefix; node != nullptr; node = node->parent) {
                const auto& segment = connection_segment_at(network, node->segment);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                if (physical_to_key(route_segment) == next) {
                    return true;
                }
            }
            return false;
        }

        auto cursor = branch.trace.parent_branch;
        while (cursor.has_value()) {
            const auto& ancestor = branch_at(branches, *cursor);
            if (ancestor.trace.current_physical == next) {
                return true;
            }
            cursor = ancestor.trace.parent_branch;
        }

        return false;
    }

    bool branch_revisits_occurrence(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , StopOccurrenceKey          next
    ) {
        if (branch.trace.current_occurrence.has_value()
            && branch.trace.current_occurrence.value() == next) {
            return true;
        }

        if (branch.od_day_carrier.support_prefix != nullptr) {
            for (auto node = branch.od_day_carrier.support_prefix; node != nullptr; node = node->parent) {
                const auto& segment = connection_segment_at(network, node->segment);
                if (!is_timed_connection(segment)) {
                    continue;
                }
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                if (occurrence_key(line_topology_of(route_segment)->to) == next) {
                    return true;
                }
            }
            return false;
        }

        auto cursor = branch.trace.parent_branch;
        while (cursor.has_value()) {
            const auto& ancestor = branch_at(branches, *cursor);
            if (ancestor.trace.current_occurrence.has_value()
                && ancestor.trace.current_occurrence.value() == next) {
                return true;
            }
            cursor = ancestor.trace.parent_branch;
        }

        return false;
    }

    CapacityExposure add_capacity_exposure(
          CapacityExposure lhs
        , CapacityExposure rhs
    ) noexcept {
        return CapacityExposure{
            Time{
                lhs.equivalent_time.value() + rhs.equivalent_time.value()
            }
        };
    }

    mathfp::Expected<CapacityExposure> timed_successor_capacity_exposure(
          const ConnectionSegment& segment
        , const RouteSegment&      route_segment
        , std::optional<IntervalId> interval
        , const SearchCostContext& search_cost
    ) {
        switch (search_cost.mode) {
            case SearchCostMode::BaseOnly:
                return CapacityExposure{ Time{ 0.0 } };

            case SearchCostMode::CapacityAware:
                if (!interval.has_value()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("capacity-aware origin-period search requires task-indexed interval exposure")
                    );
                }
                return search_capacity_exposure(
                      make_transition_ride_leg(segment, route_segment)
                    , *interval
                    , search_cost.capacity
                );
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unknown search cost mode")
                .ctx("mode", static_cast<std::int64_t>(search_cost.mode))
        );
    }

    PaperSuccessorFeasibilityDecision evaluate_paper_search_successor_feasibility(
          const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor_ref
        , const SearchTimeDomain*    first_departure_domain
        , const TransferLimits&      limits
    ) noexcept {
        const auto& successor = connection_segment_at(network, successor_ref.connection);
        const auto& route_segment = route_segment_at(network, successor.route_segment);

        if (is_walk_connection(successor)) {
            /*
             * Walk connection segments are always available in the paper
             * preprocessing model. They do not have departure times; the
             * extension only advances the current prefix time by walk
             * duration in extend_metrics_with_walk.
             */
            if (!is_branch_extension_feasible(
                  feasibility_state(branch)
                , successor
                , route_segment
                , limits
            )) {
                return PaperSuccessorFeasibilityDecision{
                    .rejection = PaperSuccessorFeasibilityRejection::BranchFeasibility
                };
            }
            return PaperSuccessorFeasibilityDecision{};
        }

        if (!first_timed_departure_allowed(
              branch
            , successor
            , first_departure_domain
            , limits
        )) {
            return PaperSuccessorFeasibilityDecision{
                .rejection = PaperSuccessorFeasibilityRejection::FirstDepartureDomain
            };
        }
        if (!is_branch_extension_feasible(
              feasibility_state(branch)
            , successor
            , route_segment
            , limits
        )) {
            return PaperSuccessorFeasibilityDecision{
                .rejection = PaperSuccessorFeasibilityRejection::BranchFeasibility
            };
        }
        if (!improves_repeated_stop_reboarding(
              branch
            , network
            , successor
            , route_segment
        )) {
            return PaperSuccessorFeasibilityDecision{
                .rejection = PaperSuccessorFeasibilityRejection::Reboarding
            };
        }
        return PaperSuccessorFeasibilityDecision{};
    }

    SearchPartialMetrics extend_metrics_with_walk(
          SearchPartialMetrics metrics
        , const RouteSegment&   route_segment
        , ConnectionLegKind     kind
    ) {
        switch (kind) {
            case ConnectionLegKind::AccessWalk:
                metrics.access_time = Time{
                    metrics.access_time.value() + route_segment.run_time.value()
                };
                return metrics;

            case ConnectionLegKind::TransferWalk:
                metrics.current_time = Time{
                    metrics.current_time->value() + route_segment.run_time.value()
                };
                metrics.transfer_walk_time = Time{
                    metrics.transfer_walk_time.value() + route_segment.run_time.value()
                };
                return metrics;

            case ConnectionLegKind::EgressWalk:
                metrics.current_time = Time{
                    metrics.current_time->value() + route_segment.run_time.value()
                };
                metrics.egress_time = Time{
                    metrics.egress_time.value() + route_segment.run_time.value()
                };
                return metrics;

            case ConnectionLegKind::Ride:
            case ConnectionLegKind::InitialWait:
            case ConnectionLegKind::TransferWait:
            case ConnectionLegKind::FinalWait:
                return metrics;
        }

        return metrics;
    }

    mathfp::Expected<PaperConnectionPrefixEvaluation>
    evaluate_paper_connection_prefix_before_branch(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor_ref
        , std::optional<IntervalId>  interval
        , const SearchCostContext&   search_cost
    ) {
        const auto& successor = connection_segment_at(network, successor_ref.connection);
        const auto& route_segment = route_segment_at(network, successor.route_segment);

        auto metrics = branch.metrics;
        auto next_physical = physical_to_key(route_segment);

        if (is_walk_connection(successor)) {
            if (!successor_ref.walk_transition.has_value()) {
                return PaperConnectionPrefixEvaluation{
                    .rejection = BranchTransitionRejection::MissingWalkTransition
                };
            }
            if (branch_revisits_physical(branches, branch, network, next_physical)) {
                return PaperConnectionPrefixEvaluation{
                    .rejection = BranchTransitionRejection::RepeatedPhysicalNode
                };
            }
            metrics = extend_metrics_with_walk(
                  std::move(metrics)
                , route_segment
                , successor_ref.walk_transition->kind
            );
        } else {
            if (!timed_extension_transition(branch.trace.phase).has_value()) {
                return PaperConnectionPrefixEvaluation{
                    .rejection = BranchTransitionRejection::InvalidTimedPhase
                };
            }
            const auto next_occurrence = occurrence_key(
                line_topology_of(route_segment)->to
            );
            if (branch_revisits_occurrence(branches, branch, network, next_occurrence)) {
                return PaperConnectionPrefixEvaluation{
                    .rejection = BranchTransitionRejection::RepeatedStopOccurrence
                };
            }
            MATHFP_TRY_LET(
                  CapacityExposure
                , capacity_exposure
                , timed_successor_capacity_exposure(
                      successor
                    , route_segment
                    , interval
                    , search_cost
                )
            );
            MATHFP_TRY_ASSIGN(
                metrics,
                extend_metrics_with_timed(
                  std::move(metrics)
                , successor
                , capacity_exposure
            ));
        }

        MATHFP_TRY_LET(
              SearchPruningMetrics
            , pruning_metrics
            , make_day_path_pruning_metrics(metrics, search_cost)
        );
        return PaperConnectionPrefixEvaluation{
              .kind = PaperConnectionPrefixKind::ConnectionPrefix
            , .connection_candidate = PaperConnectionCandidateMetrics{
                  .node = PaperConnectionNodeKey{ .physical = next_physical }
                , .metrics = std::move(pruning_metrics)
            }
        };
    }

    mathfp::Expected<SearchPartialMetrics> extend_metrics_with_timed(
          SearchPartialMetrics      metrics
        , const ConnectionSegment&  segment
        , CapacityExposure          capacity_exposure
    ) {
        const auto had_departure = metrics.departure.has_value();
        if (!had_departure) {
            metrics.departure = Time{
                segment.departure->value() - metrics.access_time.value()
            };
        } else {
            metrics.transfer_wait_time = Time{
                metrics.transfer_wait_time.value()
                + (segment.departure->value() - metrics.current_time->value())
            };
            MATHFP_TRY_LET(
                  TransferCount
                , next_transfers
                , next_transfer_count(metrics.transfers)
            );
            metrics.transfers = next_transfers;
        }

        metrics.in_vehicle_time = Time{
            metrics.in_vehicle_time.value()
            + (segment.arrival->value() - segment.departure->value())
        };
        metrics.current_time = *segment.arrival;
        metrics.fare = metrics.fare + segment.fare.value_or(0.0);
        metrics.capacity_exposure = add_capacity_exposure(
              metrics.capacity_exposure
            , capacity_exposure
        );
        return metrics;
    }

    mathfp::Expected<BranchTransitionResult> transition_search_branch_with_diagnostics(
          const BranchArena&         branches
        , std::size_t                branch_index
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor_ref
        , std::optional<IntervalId>  interval
        , const SearchCostContext&   search_cost
    ) {
        const auto& successor = connection_segment_at(network, successor_ref.connection);
        const auto& route_segment = route_segment_at(network, successor.route_segment);
        if (is_walk_connection(successor)) {
            if (!successor_ref.walk_transition.has_value()) {
                return BranchTransitionResult{
                    .diagnostics = BranchTransitionDiagnostics{
                        .rejection = BranchTransitionRejection::MissingWalkTransition
                    }
                };
            }
            const auto next_physical = physical_to_key(route_segment);
            if (branch_revisits_physical(branches, branch, network, next_physical)) {
                return BranchTransitionResult{
                    .diagnostics = BranchTransitionDiagnostics{
                        .rejection = BranchTransitionRejection::RepeatedPhysicalNode
                    }
                };
            }
            return BranchTransitionResult{
                .branch = SearchBranch{
                      .trace = extend_trace_with_walk(
                            branch.trace
                          , branch_index
                          , route_segment
                          , successor.id
                          , successor_ref.day_level_edge
                          , *successor_ref.walk_transition
                      )
                    , .metrics = extend_metrics_with_walk(
                            branch.metrics
                          , route_segment
                          , successor_ref.walk_transition->kind
                      )
                    , .od_day_carrier = branch.od_day_carrier
                    , .paper_connection_label = branch.paper_connection_label
                }
            };
        }

        const auto next_phase = timed_extension_transition(branch.trace.phase);
        if (!next_phase.has_value()) {
            return BranchTransitionResult{
                .diagnostics = BranchTransitionDiagnostics{
                    .rejection = BranchTransitionRejection::InvalidTimedPhase
                }
            };
        }
        const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
        if (branch_revisits_occurrence(branches, branch, network, next_occurrence)) {
            return BranchTransitionResult{
                .diagnostics = BranchTransitionDiagnostics{
                    .rejection = BranchTransitionRejection::RepeatedStopOccurrence
                }
            };
        }
        MATHFP_TRY_LET(
              CapacityExposure
            , capacity_exposure
            , timed_successor_capacity_exposure(
                  successor
                , route_segment
                , interval
                , search_cost
            )
        );
        MATHFP_TRY_LET(
              SearchPartialMetrics
            , extended_metrics
            , extend_metrics_with_timed(
                  branch.metrics
                , successor
                , capacity_exposure
            )
        );
        return BranchTransitionResult{
            .branch = SearchBranch{
                  .trace = extend_trace_with_timed(
                        branch.trace
                      , branch_index
                      , successor
                      , route_segment
                      , *next_phase
                      , successor_ref.day_level_edge
                  )
                , .metrics = extended_metrics
                , .od_day_carrier = branch.od_day_carrier
                , .paper_connection_label = branch.paper_connection_label
            }
        };
    }

    mathfp::Expected<std::optional<SearchBranch>> transition_search_branch(
          const BranchArena&         branches
        , std::size_t                branch_index
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const SearchSuccessor&     successor
        , std::optional<IntervalId>  interval
        , const SearchCostContext&   search_cost
    ) {
        MATHFP_TRY_LET(
              BranchTransitionResult
            , result
            , transition_search_branch_with_diagnostics(
                  branches
                , branch_index
                , branch
                , network
                , successor
                , interval
                , search_cost
            )
        );
        return std::move(result.branch);
    }

}  // namespace timetable::domain::assignment
