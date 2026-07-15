#include "timetable/domain/assignment/search/projection/complete_connection.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"
#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"
#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] Time add_time(
              Time lhs
            , Time rhs
        ) noexcept {
            return Time{ lhs.value() + rhs.value() };
        }

        [[nodiscard]] ConnectionLeg make_walk_leg(
              ConnectionLegKind   kind
            , ConnectionSegmentId segment_id
            , const RouteSegment& route_segment
            , Time                start_time
        ) {
            return ConnectionLeg{
                  .kind               = kind
                , .connection_segment = segment_id
                , .route_segment      = route_segment.id
                , .physical_from      = physical_from_key(route_segment)
                , .physical_to        = physical_to_key(route_segment)
                , .occurrence_from    = std::nullopt
                , .occurrence_to      = std::nullopt
                , .line               = std::nullopt
                , .route              = std::nullopt
                , .trip               = std::nullopt
                , .start_time         = start_time
                , .end_time           = add_time(start_time, route_segment.run_time)
                , .length             = route_segment.length
                , .fare               = 0.0
            };
        }

        [[nodiscard]] std::optional<ConnectionLeg> make_transfer_wait_leg(
              Time                        current_time
            , const ConnectionSegment&    segment
            , EndpointKey                 physical
        ) noexcept {
            if (!segment.departure.has_value()) {
                return std::nullopt;
            }
            if (segment.departure->value() <= current_time.value()) {
                return std::nullopt;
            }
            return ConnectionLeg{
                  .kind               = ConnectionLegKind::TransferWait
                , .connection_segment = std::nullopt
                , .route_segment      = std::nullopt
                , .physical_from      = physical
                , .physical_to        = physical
                , .occurrence_from    = std::nullopt
                , .occurrence_to      = std::nullopt
                , .line               = std::nullopt
                , .route              = std::nullopt
                , .trip               = std::nullopt
                , .start_time         = current_time
                , .end_time           = *segment.departure
                , .length             = Length{ 0.0 }
                , .fare               = 0.0
            };
        }

        [[nodiscard]] ConnectionLeg make_ride_leg(
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

        [[nodiscard]] std::optional<std::size_t> first_timed_segment_position(
              const PreprocessedNetwork&          network
            , std::span<const ConnectionSegmentId> segments
        ) {
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (!is_walk_connection(connection_segment_at(network, segments[i]))) {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] Time access_time_before_first_timed(
              const PreprocessedNetwork&          network
            , std::span<const ConnectionSegmentId> segments
            , std::size_t                         first_timed_pos
        ) {
            Time access_time{ 0.0 };
            for (std::size_t i = 0; i < first_timed_pos; ++i) {
                const auto& segment = connection_segment_at(network, segments[i]);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                access_time = add_time(access_time, route_segment.run_time);
            }
            return access_time;
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const CompactCompleteConnectionRetention& retention
        ) noexcept {
            CompleteConnectionMetricSummary summary;
            for (const auto& metrics : retention.metrics) {
                if (summary.empty) {
                    summary.empty = false;
                    summary.min_impedance = metrics.impedance;
                    summary.min_journey_time = metrics.journey_time.value();
                    summary.min_transfers = static_cast<double>(metrics.transfers.get());
                    continue;
                }
                summary.min_impedance = std::min(summary.min_impedance, metrics.impedance);
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(metrics.transfers.get())
                );
            }
            return summary;
        }

        [[nodiscard]] mathfp::Expected<IntervalId> complete_retention_interval(
              const SearchProjectionSlot& slot
            , const SearchCostContext&    search_cost
        ) {
            if (slot.interval.has_value()) {
                return *slot.interval;
            }
            if (search_cost.mode != SearchCostMode::BaseOnly) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("completion-target projection currently requires base search cost")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
            return IntervalId{ 0 };
        }

        mathfp::Expected<CompleteProjectionRetentionDiagnostics>
        retain_timed_witness_as_day_path(
              SearchConnection            timed_witness
            , DayPathSignature            finalized_signature
            , const SearchProjectionSlot& slot
            , const SearchCostContext&    search_cost
            , SearchProjectionRetention&  retention
        ) {
            const auto materialized_signature = day_path_signature_of(timed_witness);
            if (finalized_signature != materialized_signature) {
                return mathfp::unexpected(
                    mathfp::internal_error("incremental OD-day path prefix disagrees with materialized connection")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
            if (finalized_signature.origin != slot.origin
                || finalized_signature.destination != slot.destination) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day post-layer path signature does not match projection slot")
                        .ctx("slot_origin", slot.origin.get())
                        .ctx("slot_destination", slot.destination.get())
                        .ctx("path_origin", finalized_signature.origin.get())
                        .ctx("path_destination", finalized_signature.destination.get())
                );
            }

            MATHFP_TRY_LET(
                  IntervalId
                , interval
                , complete_retention_interval(slot, search_cost)
            );
            MATHFP_TRY_LET(
                  DayPathRetentionDecision
                , decision
                , retain_day_path_alternative(
                  retention.day_paths
                , std::move(finalized_signature)
                , std::move(timed_witness)
                , search_cost
                , interval
                , DayPathRetentionConfig{}
                )
            );

            return CompleteProjectionRetentionDiagnostics{
                  .post_layer_day_path_candidates = 1u
                , .post_layer_day_path_inserted =
                    decision.inserted_path ? 1u : 0u
                , .post_layer_day_path_representative_replaced =
                    decision.replaced_representative ? 1u : 0u
                , .post_layer_day_path_supports = decision.timed_connection_count
            };
        }

        mathfp::Expected<CompleteProjectionRetentionDiagnostics>
        retain_od_day_completed_branch_as_day_path(
              const SearchBranch&        branch
            , const BranchArena&         branches
            , const PreprocessedNetwork& network
            , const TransferLimits&      transfer_limits
            , const SearchCostContext&   search_cost
            , const SearchProjectionSlot& slot
            , SearchProjectionRetention& retention
        ) {
            if (!complete_branch_can_finish(
                  branch
                , network
                , transfer_limits
                , slot.destination
            )) {
                return CompleteProjectionRetentionDiagnostics{};
            }

            auto diagnostics = CompleteProjectionRetentionDiagnostics{
                .completed_connections = 1u
            };
            auto finalized_signature = make_day_path_signature_from_tree_label(
                  materialize_day_path_prefix(branch.od_day_carrier.path_identity)
                , slot.destination
            );
            MATHFP_TRY_LET(
                  std::optional<SearchConnection>
                , timed_witness
                , complete_connection(
                      branches
                    , branch
                    , network
                    , transfer_limits
                    , slot.destination
                  )
            );
            if (!timed_witness.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day completed branch cannot be materialized as timed witness")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
            MATHFP_TRY_LET(
                  CompleteProjectionRetentionDiagnostics
                , day_path_diagnostics
                , retain_timed_witness_as_day_path(
                      std::move(*timed_witness)
                    , std::move(finalized_signature)
                    , slot
                    , search_cost
                    , retention
                  )
            );
            diagnostics.post_layer_day_path_candidates =
                day_path_diagnostics.post_layer_day_path_candidates;
            diagnostics.post_layer_day_path_inserted =
                day_path_diagnostics.post_layer_day_path_inserted;
            diagnostics.post_layer_day_path_representative_replaced =
                day_path_diagnostics.post_layer_day_path_representative_replaced;
            diagnostics.post_layer_day_path_supports =
                day_path_diagnostics.post_layer_day_path_supports;
            return diagnostics;
        }

    }  // namespace

    std::vector<ConnectionSegmentId> branch_connection_segments(
          const BranchArena&  branches
        , const SearchBranch& branch
    ) {
        if (branch.od_day_carrier.support_prefix != nullptr) {
            return materialize_od_day_support_segments(
                branch.od_day_carrier.support_prefix
            );
        }

        std::vector<ConnectionSegmentId> reversed_segments;
        const auto* cursor = &branch;
        while (cursor->trace.incoming_segment.has_value()) {
            reversed_segments.push_back(*cursor->trace.incoming_segment);
            if (!cursor->trace.parent_branch.has_value()) {
                break;
            }
            cursor = &branch_at(branches, *cursor->trace.parent_branch);
        }
        std::reverse(reversed_segments.begin(), reversed_segments.end());
        return reversed_segments;
    }

    mathfp::Expected<ConnectionTrace> materialize_connection_trace(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
    ) {
        const auto segments = branch_connection_segments(branches, branch);
        const auto first_timed_pos = first_timed_segment_position(network, segments);
        if (!first_timed_pos.has_value()) {
            return mathfp::unexpected(
                mathfp::internal_error("completed search branch has no timed segment")
                    .ctx("origin", branch.trace.origin.get())
                    .ctx("endpoint_id", branch.trace.current_physical.id)
            );
        }

        const auto& first_timed_segment = connection_segment_at(
              network
            , segments[*first_timed_pos]
        );
        if (!first_timed_segment.departure.has_value()) {
            return mathfp::unexpected(
                mathfp::internal_error("first timed segment has no departure while materializing trace")
                    .ctx("segment", first_timed_segment.id.get())
            );
        }

        auto cursor = Time{
            first_timed_segment.departure->value()
            - access_time_before_first_timed(
                  network
                , segments
                , *first_timed_pos
              ).value()
        };
        auto current_physical = endpoint_key(branch.trace.origin);
        bool after_first_timed = false;
        ConnectionTrace trace;
        trace.legs.reserve(segments.size() + branch.metrics.transfers.get());

        for (const auto segment_id : segments) {
            const auto& segment = connection_segment_at(network, segment_id);
            const auto& route_segment = route_segment_at(network, segment.route_segment);
            if (is_walk_connection(segment)) {
                const auto next_physical = physical_to_key(route_segment);
                const auto kind = after_first_timed
                    ? (next_physical.kind == EndpointKind::Zone
                        ? ConnectionLegKind::EgressWalk
                        : ConnectionLegKind::TransferWalk)
                    : ConnectionLegKind::AccessWalk;
                auto leg = make_walk_leg(kind, segment_id, route_segment, cursor);
                cursor = leg.end_time;
                current_physical = leg.physical_to;
                trace.legs.push_back(std::move(leg));
                continue;
            }

            if (after_first_timed) {
                if (auto wait_leg = make_transfer_wait_leg(
                      cursor
                    , segment
                    , current_physical
                )) {
                    cursor = wait_leg->end_time;
                    trace.legs.push_back(*wait_leg);
                }
            }

            auto ride_leg = make_ride_leg(segment, route_segment);
            cursor = ride_leg.end_time;
            current_physical = ride_leg.physical_to;
            after_first_timed = true;
            trace.legs.push_back(std::move(ride_leg));
        }

        return trace;
    }

    mathfp::Expected<std::optional<SearchConnection>> complete_connection(
          const BranchArena&         branches
        , const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const TransferLimits&      limits
        , ZoneId                     destination
    ) {
        if (!is_complete_connection(branch, destination)) {
            return std::nullopt;
        }

        if (!limits.allow_end_wait
            && branch.trace.last_timed_segment != nullptr
            && branch.trace.incoming_segment.has_value()) {
            const auto& last_segment = connection_segment_at(
                  network
                , branch.trace.incoming_segment.value()
            );
            if (is_walk_connection(last_segment)) {
                return std::nullopt;
            }
        }

        MATHFP_TRY_LET(
              ConnectionTrace
            , trace
            , materialize_connection_trace(branches, branch, network)
        );
        MATHFP_TRY_LET(
              SearchConnection
            , connection
            , make_search_connection(
                  branch.trace.origin
                , destination
                , std::move(trace)
            )
        );
        return std::optional<SearchConnection>{ std::move(connection) };
    }

    bool complete_branch_can_finish(
          const SearchBranch&        branch
        , const PreprocessedNetwork& network
        , const TransferLimits&      limits
        , ZoneId                     destination
    ) {
        if (!is_complete_connection(branch, destination)) {
            return false;
        }

        if (!limits.allow_end_wait
            && branch.trace.last_timed_segment != nullptr
            && branch.trace.incoming_segment.has_value()) {
            const auto& last_segment = connection_segment_at(
                  network
                , branch.trace.incoming_segment.value()
            );
            if (is_walk_connection(last_segment)) {
                return false;
            }
        }

        return true;
    }

    mathfp::Expected<CompleteConnectionMetrics>
    complete_connection_metrics_from_branch(
          const SearchBranch&       branch
        , const SearchCostContext&  search_cost
    ) {
        if (!branch.metrics.departure.has_value()
            || !branch.metrics.current_time.has_value()) {
            return mathfp::unexpected(
                mathfp::internal_error("completed branch metrics miss departure or arrival")
                    .ctx("origin", branch.trace.origin.get())
            );
        }

        const auto components = SearchCostComponents{
              .base = partial_impedance_components(branch.metrics)
            , .capacity_exposure = branch.metrics.capacity_exposure
        };
        MATHFP_TRY_LET(
              double
            , impedance
            , search_impedance(components, search_cost)
        );
        return CompleteConnectionMetrics{
              .departure   = *branch.metrics.departure
            , .arrival     = *branch.metrics.current_time
            , .journey_time = partial_journey_time(branch.metrics)
            , .transfers    = branch.metrics.transfers
            , .impedance    = impedance
        };
    }

    std::size_t finalize_compact_complete_connection_count(
          const CompactCompleteConnectionRetention& retention
        , const ChoiceTolerances&                   tolerances
        , ChoiceRolloutStage                        rollout_stage
    ) noexcept {
        if (rollout_stage == ChoiceRolloutStage::ExactOnly) {
            return retention.metrics.size();
        }

        const auto summary = summarize_complete_metrics(retention);
        std::size_t count = 0;
        for (const auto& metrics : retention.metrics) {
            if (within_complete_connection_tolerances(
                  metrics
                , summary
                , tolerances
            )) {
                ++count;
            }
        }
        return count;
    }

    mathfp::Expected<CompleteConnectionRetentionDecision>
    retain_exact_compact_complete_connection(
          CompactCompleteConnectionRetention& retention
        , const BranchArena&                  branches
        , const SearchBranch&                 branch
        , CompleteConnectionMetrics           metrics
        , const CompleteConnectionDominanceConfig& dominance_config
    ) {
        for (const auto& known : retention.metrics) {
            if (complete_connection_dominates(
                  dominance_config
                , known
                , metrics
            )) {
                return CompleteConnectionRetentionDecision{
                      .accepted          = false
                    , .removed_dominated = 0
                };
            }
        }

        auto trace = branch_connection_segments(branches, branch);
        for (const auto& known : retention.traces) {
            if (known == trace) {
                return CompleteConnectionRetentionDecision{
                      .accepted          = false
                    , .removed_dominated = 0
                };
            }
        }

        const auto before = retention.metrics.size();
        std::size_t write = 0;
        for (std::size_t read = 0; read < retention.metrics.size(); ++read) {
            if (complete_connection_dominates(
                  dominance_config
                , metrics
                , retention.metrics[read]
            )) {
                continue;
            }
            if (write != read) {
                retention.metrics[write] = retention.metrics[read];
                retention.traces[write]  = std::move(retention.traces[read]);
            }
            ++write;
        }
        retention.metrics.resize(write);
        retention.traces.resize(write);
        const auto removed = before - retention.metrics.size();
        retention.metrics.push_back(metrics);
        retention.traces.push_back(std::move(trace));
        return CompleteConnectionRetentionDecision{
              .accepted          = true
            , .removed_dominated = removed
        };
    }

    mathfp::Expected<CompleteProjectionRetentionDiagnostics>
    retain_complete_projection_for_slot(
          const SearchBranch&        branch
        , const BranchArena&         branches
        , const PreprocessedNetwork& network
        , const TransferLimits&      transfer_limits
        , const SearchCostContext&   search_cost
        , const SearchProjectionSlot& slot
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const CompleteConnectionDominanceConfig& dominance_config
        , SearchProjectionRetention& retention
    ) {
        if (slot.kind == SearchProjectionSlotKind::CompletionTarget) {
            if (!complete_branch_can_finish(
                  branch
                , network
                , transfer_limits
                , slot.destination
            )) {
                return CompleteProjectionRetentionDiagnostics{};
            }

            auto diagnostics = CompleteProjectionRetentionDiagnostics{
                .completed_connections = 1u
            };
            MATHFP_TRY_LET(
                  CompleteConnectionMetrics
                , metrics
                , complete_connection_metrics_from_branch(
                      branch
                    , search_cost
                  )
            );
            MATHFP_TRY_LET(
                  CompleteConnectionRetentionDecision
                , retention_decision
                , retain_exact_compact_complete_connection(
                      retention.compact_complete_connections
                    , branches
                    , branch
                    , metrics
                    , dominance_config
                  )
            );
            diagnostics.removed_complete_dominated =
                retention_decision.removed_dominated;
            if (!retention_decision.accepted) {
                diagnostics.rejected_complete_dominance = 1u;
            }
            return diagnostics;
        }

        if (slot.kind == SearchProjectionSlotKind::OdDayPair) {
            return retain_od_day_completed_branch_as_day_path(
                  branch
                , branches
                , network
                , transfer_limits
                , search_cost
                , slot
                , retention
            );
        }

        MATHFP_TRY_LET(
              std::optional<SearchConnection>
            , complete
            , complete_connection(
                  branches
                , branch
                , network
                , transfer_limits
                , slot.destination
              )
        );
        if (!complete.has_value()) {
            return CompleteProjectionRetentionDiagnostics{};
        }

        auto diagnostics = CompleteProjectionRetentionDiagnostics{
            .completed_connections = 1u
        };
        if (slot.kind == SearchProjectionSlotKind::DemandTask) {
            if (!slot.task.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("demand projection slot has no task while retaining complete connection")
                );
            }
            const auto& task = slot.task->get();
            const auto connection_metrics = metrics_of(*complete);
            if (!connection_admissible_for_demand_segment(
                  connection_metrics
                , task.interval
                , assignment_period
                , admissibility_config
            )) {
                diagnostics.rejected_complete_admissibility = 1u;
                return diagnostics;
            }
        }
        MATHFP_TRY_LET(
              IntervalId
            , interval
            , complete_retention_interval(slot, search_cost)
        );
        MATHFP_TRY_LET(
              CompleteConnectionRetentionDecision
            , retention_decision
            , retain_exact_complete_connection(
                  retention.complete_connections
                , std::move(*complete)
                , search_cost
                , interval
                , dominance_config
              )
        );
        diagnostics.removed_complete_dominated =
            retention_decision.removed_dominated;
        if (!retention_decision.accepted) {
            diagnostics.rejected_complete_dominance = 1u;
        }
        return diagnostics;
    }

}  // namespace timetable::domain::assignment
