#include "detail/output_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/skim.hpp"

namespace timetable::domain::assignment::detail {

    namespace {

        using ChosenConnectionIndexMap = std::map<grouping::ConnectionTraceKey, std::size_t>;
        using TaskConnectionTraceMap = std::map<grouping::DemandKey, std::map<grouping::ConnectionTraceKey, bool>>;
        using OdDayPathMap = std::map<grouping::OdKey, std::map<DayPathSignature, bool>>;
        using OdDayPaperSplitSummaryMap = std::map<grouping::OdKey, AssignmentOdPaperSplitSummary>;
        using UnassignedDemandByKey = std::map<grouping::DemandKey, std::vector<const UnassignedDemand*>>;

        AssignmentOdResult make_empty_od_result(
            const grouping::OdKey& od
        ) {
            return AssignmentOdResult{
                  .origin                  = od.origin
                , .destination             = od.destination
                , .total_demand_passengers = 0.0
                , .assigned_passengers     = 0.0
                , .connections             = {}
                , .intervals               = {}
                , .diagnostics             = {}
                , .paper_split             = {}
            };
        }

        double total_input_demand(
            const InputModel& input
        ) {
            return mathfp::compensated_sum_by(input.demand, [](const DemandEntry& demand) {
                return demand.passengers;
            });
        }

        double total_assigned_passengers(
            const DemandSplitResult& split_result
        ) {
            return mathfp::compensated_sum_by(split_result.shares, [](const ConnectionDemandShare& share) {
                return share.passengers;
            });
        }

        double total_unassigned_passengers(
            const DemandSplitResult& split_result
        ) {
            return mathfp::compensated_sum_by(split_result.unassigned, [](const UnassignedDemand& demand) {
                return demand.passengers;
            });
        }

        bool almost_equal_scalar(
              double lhs
            , double rhs
        ) noexcept {
            return mathfp::almost_equal(lhs, rhs);
        }

        TaskConnectionTraceMap build_task_connection_trace_map(
            const ConnectionChoiceResult& choice_result
        ) {
            TaskConnectionTraceMap traces;
            for (const auto& task_result : choice_result.task_results) {
                auto& task_traces = traces[grouping::DemandKey{
                      .origin      = task_result.task.origin
                    , .destination = task_result.task.destination
                    , .interval    = task_result.task.interval.id
                }];
                for (const auto& connection : task_result.connections) {
                    task_traces[grouping::connection_trace_key(connection)] = true;
                }
            }
            return traces;
        }

        OdDayPathMap build_od_day_path_map(
            const OdDayPathChoiceResult& choice_result
        ) {
            OdDayPathMap paths;
            for (const auto& origin_result : choice_result.origin_results) {
                for (const auto& pair_result : origin_result.pair_results) {
                    auto& od_paths = paths[grouping::OdKey{
                          .origin      = pair_result.origin
                        , .destination = pair_result.destination
                    }];
                    if (!pair_result.alternatives.empty()) {
                        for (const auto& alternative : pair_result.alternatives) {
                            od_paths[day_path_signature_of(alternative)] = true;
                        }
                    } else {
                        for (const auto& connection : pair_result.connections) {
                            od_paths[day_path_signature_of(connection)] = true;
                        }
                    }
                }
            }
            return paths;
        }

        [[nodiscard]] bool share_support_interval_admissible(
              const ConnectionDemandShare&       share
            , const TimeInterval&                interval
            , const AssignmentPeriodConfig&      assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
        ) {
            return share.day_path_support.has_value()
                && connection_admissible_for_demand_segment(
                      share.day_path_support->connection_metrics
                    , interval
                    , assignment_period
                    , admissibility_config
                );
        }

        mathfp::Expected<OdDayPaperSplitSummaryMap> build_od_day_paper_split_summary_map(
              const OdDayPathChoiceResult& choice_result
            , const DemandSplitResult&     split_result
        ) {
            OdDayPaperSplitSummaryMap summaries;

            for (const auto& origin_result : choice_result.origin_results) {
                for (const auto& pair_result : origin_result.pair_results) {
                    auto& summary = summaries[grouping::OdKey{
                          .origin      = pair_result.origin
                        , .destination = pair_result.destination
                    }];
                    if (!pair_result.alternatives.empty()) {
                        summary.structural_day_path_count += pair_result.alternatives.size();
                    } else {
                        std::map<DayPathSignature, bool> compact_paths;
                        for (const auto& connection : pair_result.connections) {
                            compact_paths[day_path_signature_of(connection)] = true;
                        }
                        summary.structural_day_path_count += compact_paths.size();
                    }
                }
            }

            for (const auto& certificate : split_result.od_day_paper_split) {
                auto& summary = summaries[grouping::OdKey{
                      .origin      = certificate.origin
                    , .destination = certificate.destination
                }];
                summary.timed_support_alternative_count = std::max(
                      summary.timed_support_alternative_count
                    , certificate.candidate_support_count
                );
                summary.interval_admissible_split_alternative_count +=
                    certificate.interval_admissible_support_count;
            }

            for (const auto& unassigned : split_result.unassigned) {
                auto& summary = summaries[grouping::OdKey{
                      .origin      = unassigned.origin
                    , .destination = unassigned.destination
                }];
                ++summary.unassigned_demand_count;
                summary.unassigned_passengers += unassigned.passengers;
            }

            return summaries;
        }

        [[nodiscard]] AssignmentOdPaperSplitSummary summarize_paper_split_totals(
            const OdDayPaperSplitSummaryMap& summaries
        ) noexcept {
            AssignmentOdPaperSplitSummary total;
            for (const auto& [_, summary] : summaries) {
                total.structural_day_path_count += summary.structural_day_path_count;
                total.timed_support_alternative_count += summary.timed_support_alternative_count;
                total.interval_admissible_split_alternative_count +=
                    summary.interval_admissible_split_alternative_count;
                total.unassigned_demand_count += summary.unassigned_demand_count;
                total.unassigned_passengers += summary.unassigned_passengers;
            }
            return total;
        }

        std::map<grouping::OdKey, std::size_t> build_od_day_search_count_map(
            const OdDayPathSearchSummary& search_summary
        ) {
            std::map<grouping::OdKey, std::size_t> counts;
            for (const auto& pair_count : search_summary.pair_counts) {
                counts[grouping::OdKey{
                      .origin      = pair_count.origin
                    , .destination = pair_count.destination
                }] += pair_count.connection_count;
            }
            return counts;
        }

        mathfp::Expected<mathfp::Unit> validate_choice_flat_projection(
            const ConnectionChoiceResult& choice_result
        ) {
            std::map<grouping::ConnectionTraceKey, bool> task_traces;
            for (const auto& task_result : choice_result.task_results) {
                for (const auto& connection : task_result.connections) {
                    task_traces[grouping::connection_trace_key(connection)] = true;
                }
            }

            const auto flat_trace_map = grouping::trace_index_map(choice_result.connections);
            for (const auto& [trace, _] : task_traces) {
                if (!flat_trace_map.contains(trace)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: flat choice projection misses a task-local connection")
                    );
                }
            }
            for (const auto& [trace, _] : flat_trace_map) {
                if (!task_traces.contains(trace)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: flat choice projection contains a non-task connection")
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_choice_flat_projection(
            const OdDayPathChoiceResult& choice_result
        ) {
            std::map<grouping::ConnectionTraceKey, bool> pair_traces;
            for (const auto& origin_result : choice_result.origin_results) {
                for (const auto& pair_result : origin_result.pair_results) {
                    if (!pair_result.alternatives.empty()
                        && pair_result.alternatives.size() != pair_result.connections.size()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("output input: OD-day choice alternatives disagree with representative projection size")
                                .ctx("origin"       , pair_result.origin.get())
                                .ctx("destination"  , pair_result.destination.get())
                                .ctx(
                                      "alternative_count"
                                    , static_cast<std::int64_t>(pair_result.alternatives.size())
                                  )
                                .ctx(
                                      "representative_count"
                                    , static_cast<std::int64_t>(pair_result.connections.size())
                                  )
                        );
                    }
                    if (!pair_result.alternatives.empty()) {
                        for (std::size_t i = 0; i < pair_result.alternatives.size(); ++i) {
                            const auto& alternative = pair_result.alternatives[i];
                            const auto& representative = pair_result.connections[i];
                            if (grouping::connection_trace_key(day_path_representative_connection(alternative))
                                    != grouping::connection_trace_key(representative)
                                || day_path_signature_of(alternative) != day_path_signature_of(representative)) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("output input: OD-day choice representative is not the projection of its day path")
                                        .ctx("origin"     , pair_result.origin.get())
                                        .ctx("destination", pair_result.destination.get())
                                        .ctx("path_index" , static_cast<std::int64_t>(i))
                                );
                            }
                            for (const auto& support : day_path_split_support_descriptors(alternative)) {
                                if (day_path_signature_of(alternative)
                                    != support.signature) {
                                    return mathfp::unexpected(
                                        mathfp::internal_error("output input: OD-day compact support is not a support of its day-path identity")
                                            .ctx("origin"     , pair_result.origin.get())
                                            .ctx("destination", pair_result.destination.get())
                                            .ctx("path_index" , static_cast<std::int64_t>(i))
                                    );
                                }
                            }
                        }
                    }
                    for (const auto& connection : pair_result.connections) {
                        pair_traces[grouping::connection_trace_key(connection)] = true;
                    }
                }
            }

            const auto flat_trace_map = grouping::trace_index_map(choice_result.connections);
            for (const auto& [trace, _] : pair_traces) {
                if (!flat_trace_map.contains(trace)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: flat OD-day choice projection misses a pair-local connection")
                    );
                }
            }
            for (const auto& [trace, _] : flat_trace_map) {
                if (!pair_traces.contains(trace)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: flat OD-day choice projection contains a non-pair connection")
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_split_shares_are_task_local(
              const ConnectionChoiceResult& choice_result
            , const DemandSplitResult&      split_result
        ) {
            const auto task_traces = build_task_connection_trace_map(choice_result);
            for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
                const auto& share = split_result.shares[i];
                const auto key = grouping::demand_key(share);
                const auto task_it = task_traces.find(key);
                if (task_it == task_traces.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: split share has no matching choice task")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
                if (!task_it->second.contains(grouping::connection_trace_key(share.connection))) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: split share connection is outside its choice task")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_split_shares_are_od_day_choice_local(
              const OdDayPathChoiceResult&       choice_result
            , const DemandSplitResult&           split_result
        ) {
            const auto od_paths  = build_od_day_path_map(choice_result);
            for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
                const auto& share = split_result.shares[i];
                const auto key = grouping::OdKey{
                      .origin      = share.origin
                    , .destination = share.destination
                };
                const auto od_path_it = od_paths.find(key);
                if (od_path_it == od_paths.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: OD-day split share has no matching OD choice")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
                if (share.source != DemandShareAlternativeSource::DayPath) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: OD-day split share is not a day-path alternative")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
                if (!od_path_it->second.contains(share.day_path)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: OD-day split share path is outside its OD choice")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
                if (!share.day_path_support.has_value()
                    || !(share.day_path_support->signature == share.day_path)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("output input: OD-day split share compact support is outside its OD choice")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
            }
            return mathfp::kUnit;
        }

        double expected_segment_load_passenger_sum(
            const DemandSplitResult& split_result
        ) {
            mathfp::CompensatedSum<double> total;
            for (const auto& share : split_result.shares) {
                if (!(share.passengers > 0.0)) {
                    continue;
                }
                if (share.source == DemandShareAlternativeSource::DayPath) {
                    if (share.day_path_support.has_value()) {
                        total.add(
                              share.passengers
                            * static_cast<double>(share.day_path_support->ride_legs.size())
                        );
                    }
                    continue;
                }
                for (const auto& leg : canonical_connection(share.connection).trace.legs) {
                    if (is_ride_leg(leg.kind)) {
                        total.add(share.passengers);
                    }
                }
            }
            return total.value();
        }

        double actual_segment_load_passenger_sum(
            const AssignmentLoads& loads
        ) noexcept {
            return mathfp::compensated_sum_by(loads.segment_loads, [](const AssignmentSegmentLoad& load) {
                return load.passengers;
            });
        }

        mathfp::Expected<mathfp::Unit> validate_loads_are_split_projection(
              const DemandSplitResult& split_result
            , const AssignmentLoads&   loads
        ) {
            const auto expected = expected_segment_load_passenger_sum(split_result);
            const auto actual   = actual_segment_load_passenger_sum(loads);
            if (!almost_equal_scalar(expected, actual)) {
                return mathfp::unexpected(
                    mathfp::internal_error("output input: loads are not a passenger-segment projection of split shares")
                        .ctx("expected_passenger_segments", expected)
                        .ctx("actual_passenger_segments"  , actual)
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_production_load_contract(
              const DemandSplitResult&                  split_result
            , const ElementarySegmentLoads&             elementary_segment_loads
            , const AssignmentLoads&                     visum_loads
            , const ElementarySegmentOverloadAssessment& elementary_overload
        ) {
            MATHFP_TRY(validate_elementary_segment_load_projection(
                  split_result
                , elementary_segment_loads
            ));
            MATHFP_TRY(validate_loads_are_split_projection(split_result, visum_loads));
            MATHFP_TRY(validate_elementary_segment_overload_assessment(elementary_overload));
            return mathfp::kUnit;
        }

        std::map<grouping::OdKey, bool> collect_all_ods(
              const std::map<grouping::OdKey, std::size_t>& search_counts
            , const grouping::BorrowedOdConnectionGroups&   chosen_by_od
            , const grouping::DemandEntryGroups&            demand_by_od
        ) {
            std::map<grouping::OdKey, bool> all_ods;
            for (const auto& [od, _] : search_counts) {
                all_ods.emplace(od, true);
            }
            for (const auto& [od, _] : chosen_by_od) {
                all_ods.emplace(od, true);
            }
            for (const auto& [od, _] : demand_by_od) {
                all_ods.emplace(od, true);
            }
            return all_ods;
        }

        AssignmentOutput::Summary build_output_summary(
              const InputModel&             input
            , const ConnectionSearchResult& search_result
            , const ConnectionChoiceResult& choice_result
            , const DemandSplitResult&      split_result
        ) {
            return AssignmentOutput::Summary{
                  .demand_share_count      = split_result .shares     .size()
                , .structural_day_path_count = 0u
                , .timed_support_alternative_count = 0u
                , .interval_admissible_split_alternative_count = 0u
                , .unassigned_demand_count = 0u
                , .total_demand_passengers = total_input_demand(input)
                , .assigned_passengers     = total_assigned_passengers(split_result)
                , .unassigned_passengers   = 0.0
                , .diagnostics             = AssignmentOutput::Diagnostics{
                      .search_alternative_count = search_connection_count(search_result)
                    , .chosen_alternative_count = choice_result.connections.size()
                  }
            };
        }

        AssignmentOutput::Summary build_od_day_output_summary(
              const InputModel&                   input
            , const OdDayPathSearchSummary& search_summary
            , const OdDayPathChoiceResult&        choice_result
            , const DemandSplitResult&            split_result
            , const OdDayPaperSplitSummaryMap&     paper_split_summaries
        ) {
            const auto paper_totals = summarize_paper_split_totals(paper_split_summaries);
            return AssignmentOutput::Summary{
                  .demand_share_count      = split_result .shares     .size()
                , .structural_day_path_count =
                      paper_totals.structural_day_path_count
                , .timed_support_alternative_count =
                      paper_totals.timed_support_alternative_count
                , .interval_admissible_split_alternative_count =
                      paper_totals.interval_admissible_split_alternative_count
                , .unassigned_demand_count = split_result.unassigned.size()
                , .total_demand_passengers = total_input_demand(input)
                , .assigned_passengers     = total_assigned_passengers(split_result)
                , .unassigned_passengers   = total_unassigned_passengers(split_result)
                , .diagnostics             = AssignmentOutput::Diagnostics{
                      .search_alternative_count = search_connection_count(search_summary)
                    , .chosen_alternative_count = choice_result.connections.size()
                  }
            };
        }

        AssignmentOutput::Summary build_disabled_output_summary(
            const InputModel& input
        ) {
            return AssignmentOutput::Summary{
                  .demand_share_count      = 0
                , .structural_day_path_count = 0u
                , .timed_support_alternative_count = 0u
                , .interval_admissible_split_alternative_count = 0u
                , .unassigned_demand_count = 0u
                , .total_demand_passengers = total_input_demand(input)
                , .assigned_passengers     = 0.0
                , .unassigned_passengers   = 0.0
                , .diagnostics             = AssignmentOutput::Diagnostics{}
            };
        }

        AssignmentOutput::Summary build_all_zone_search_output_summary(
            const AllZoneConnectionSearchResult& search_result
        ) {
            std::size_t target_count = 0;
            for (const auto& tree : search_result.tree_results) {
                target_count += tree.target_results.size();
            }
            return AssignmentOutput::Summary{
                  .od_count                 = target_count
                , .demand_share_count      = 0
                , .structural_day_path_count = 0u
                , .timed_support_alternative_count = 0u
                , .interval_admissible_split_alternative_count = 0u
                , .unassigned_demand_count = 0u
                , .total_demand_passengers = 0.0
                , .assigned_passengers     = 0.0
                , .unassigned_passengers   = 0.0
                , .diagnostics             = AssignmentOutput::Diagnostics{
                      .search_alternative_count = search_connection_count(search_result)
                    , .chosen_alternative_count = 0
                  }
            };
        }

        AssignmentOutput::Summary build_timed_connection_diagnostics_output_summary(
            const ConnectionSearchResult& search_result
        ) {
            const auto search_count = search_connection_count(search_result);
            return AssignmentOutput::Summary{
                  .demand_share_count      = 0
                , .structural_day_path_count = 0u
                , .timed_support_alternative_count = 0u
                , .interval_admissible_split_alternative_count = 0u
                , .unassigned_demand_count = 0u
                , .total_demand_passengers = 0.0
                , .assigned_passengers     = 0.0
                , .unassigned_passengers   = 0.0
                , .diagnostics             = AssignmentOutput::Diagnostics{
                      .search_alternative_count = search_count
                    , .chosen_alternative_count = 0
                  }
            };
        }

        mathfp::Expected<ElementarySegmentOverloadAssessment> build_elementary_segment_overload_assessment_output(
              const ElementarySegmentLoads&          elementary_segment_loads
            , const std::vector<TimeInterval>&        intervals
            , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
            , const AssignmentExecutionConfig&        execution
        ) {
            MATHFP_TRY(validate_assignment_execution_config(execution));

            if (!execution.calculate_vehicle_journey_item_overload_assessment) {
                return make_disabled_by_config_vehicle_journey_item_overload_assessment();
            }

            MATHFP_TRY(validate_vehicle_journey_item_capacity_input(
                vehicle_journey_item_capacity
            ));

            switch (vehicle_journey_item_capacity.status) {
                case VehicleJourneyItemCapacityInputStatus::MissingInput:
                    return make_missing_capacity_input_vehicle_journey_item_overload_assessment();

                case VehicleJourneyItemCapacityInputStatus::Loaded:
                    return assess_elementary_segment_overload(
                          elementary_segment_loads
                        , vehicle_journey_item_capacity.capacities
                        , intervals
                    );
            }

            return mathfp::unexpected(
                mathfp::internal_error("unknown vehicle journey item capacity input status")
                    .ctx(
                          "status"
                        , static_cast<std::int64_t>(vehicle_journey_item_capacity.status)
                    )
            );
        }

        mathfp::Expected<mathfp::Unit> validate_timed_diagnostics_only_output_contract(
            const AssignmentOutput& output
        ) {
            if (
                   !output.loads.line_loads.empty()
                || !output.loads.route_loads.empty()
                || !output.loads.route_total_loads.empty()
                || !output.loads.trip_loads.empty()
                || !output.loads.segment_loads.empty()
                || !output.loads.stop_loads.empty()
                || !output.loads.stop_total_loads.empty()
                || !output.elementary_segment_loads.items.empty()
                || !output.vehicle_journey_item_loads.items.empty()
            ) {
                return mathfp::unexpected(
                    mathfp::internal_error("timed connection diagnostics output must not contain production loading rows")
                );
            }
            if (output.vehicle_journey_item_loads.status
                != VehicleJourneyItemOverloadAssessmentStatus::SkippedAssignmentDisabled) {
                return mathfp::unexpected(
                    mathfp::internal_error("timed connection diagnostics output must not calculate overload")
                        .ctx(
                              "status"
                            , std::string(to_string(output.vehicle_journey_item_loads.status))
                        )
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_paper_split_runtime_contract(
              const InputModel&                    input
            , const DemandSplitResult&             split_result
            , const AssignmentPeriodConfig&        assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
        ) {
            /*
             * Output-time guard for the paper split boundary. C(a) is a split
             * stage object: production may compact DayPath alternatives before
             * output assembly, so the output layer validates the compact
             * certificate written while timed supports were still available.
             */
            std::map<grouping::DemandKey, const OdDayPaperSplitCertificate*> certificate_by_key;
            for (const auto& certificate : split_result.od_day_paper_split) {
                const auto key = grouping::DemandKey{
                      .origin      = certificate.origin
                    , .destination = certificate.destination
                    , .interval    = certificate.interval
                };
                if (!certificate_by_key.emplace(key, &certificate).second) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: duplicate C(a) certificate")
                            .ctx("origin"     , certificate.origin.get())
                            .ctx("destination", certificate.destination.get())
                            .ctx("interval_id", certificate.interval.get())
                    );
                }
            }
            const auto shares_by_key = grouping::group_shares_by_demand_key(split_result.shares);
            UnassignedDemandByKey unassigned_by_key;
            for (const auto& unassigned : split_result.unassigned) {
                unassigned_by_key[grouping::demand_key(unassigned)].push_back(&unassigned);
            }

            for (const auto& demand : input.demand) {
                if (!(demand.passengers > 0.0)) {
                    continue;
                }

                MATHFP_TRY_LET(
                      const TimeInterval*
                    , interval
                    , find_interval(input, demand.interval)
                );

                const auto key = grouping::demand_key(demand);
                const auto certificate_it = certificate_by_key.find(key);
                if (certificate_it == certificate_by_key.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: missing C(a) certificate")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }
                const auto& certificate = *certificate_it->second;
                if (!almost_equal_scalar(certificate.demand_passengers, demand.passengers)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: C(a) certificate demand mismatch")
                            .ctx("origin"                , demand.origin.get())
                            .ctx("destination"           , demand.destination.get())
                            .ctx("interval_id"           , demand.interval.get())
                            .ctx("certificate_passengers", certificate.demand_passengers)
                            .ctx("demand_passengers"     , demand.passengers)
                    );
                }
                if (certificate.candidate_support_count
                    != certificate.interval_admissible_support_count
                     + certificate.interval_rejected_support_count) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: C(a) certificate support counts are inconsistent")
                            .ctx("origin"      , demand.origin.get())
                            .ctx("destination" , demand.destination.get())
                            .ctx("interval_id" , demand.interval.get())
                            .ctx(
                                  "candidate_supports"
                                , static_cast<std::int64_t>(certificate.candidate_support_count)
                              )
                            .ctx(
                                  "admissible_supports"
                                , static_cast<std::int64_t>(
                                      certificate.interval_admissible_support_count
                                  )
                              )
                            .ctx(
                                  "rejected_supports"
                                , static_cast<std::int64_t>(
                                      certificate.interval_rejected_support_count
                                  )
                              )
                    );
                }
                const auto c_a_size = certificate.interval_admissible_support_count;

                const auto shares_it = shares_by_key.find(key);
                const auto has_shares =
                    shares_it != shares_by_key.end() && !shares_it->second.empty();

                if (c_a_size > 0u && !has_shares) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: nonempty C(a) produced no demand shares")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                            .ctx("c_a_size"   , static_cast<std::int64_t>(c_a_size))
                    );
                }
                if (c_a_size == 0u && has_shares) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: empty C(a) produced demand shares")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }

                if (!has_shares) {
                    if (!unassigned_by_key.contains(key)
                        || !certificate.unassigned_reason.has_value()
                        || certificate.share_count != 0u
                        || !almost_equal_scalar(certificate.assigned_passengers, 0.0)
                        || !almost_equal_scalar(certificate.unassigned_passengers, demand.passengers)) {
                        return mathfp::unexpected(
                            mathfp::internal_error("paper split invariant failed: empty C(a) must become explicit unassigned demand")
                                .ctx("origin"     , demand.origin.get())
                                .ctx("destination", demand.destination.get())
                                .ctx("interval_id", demand.interval.get())
                        );
                    }
                    continue;
                }

                mathfp::CompensatedSum<double> probability_sum;
                mathfp::CompensatedSum<double> passenger_sum;
                for (const auto* share : shares_it->second) {
                    if (share->source != DemandShareAlternativeSource::DayPath
                        || !share->day_path_support.has_value()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("paper split invariant failed: split alternative is not a concrete timed support")
                                .ctx("origin"     , share->origin.get())
                                .ctx("destination", share->destination.get())
                                .ctx("interval_id", share->interval.get())
                        );
                    }
                    if (!share_support_interval_admissible(
                          *share
                        , *interval
                        , assignment_period
                        , admissibility_config
                    )) {
                        return mathfp::unexpected(
                            mathfp::internal_error("paper split invariant failed: elementary load share references support outside C(a)")
                                .ctx("origin"     , share->origin.get())
                                .ctx("destination", share->destination.get())
                                .ctx("interval_id", share->interval.get())
                        );
                    }
                    probability_sum.add(share->probability);
                    passenger_sum.add(share->passengers);
                }
                if (certificate.share_count != shares_it->second.size()
                    || certificate.unassigned_reason.has_value()
                    || !almost_equal_scalar(certificate.probability_sum, probability_sum.value())
                    || !almost_equal_scalar(certificate.assigned_passengers, passenger_sum.value())
                    || !almost_equal_scalar(certificate.unassigned_passengers, 0.0)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: C(a) certificate does not match shares")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                            .ctx("certificate_share_count", static_cast<std::int64_t>(certificate.share_count))
                            .ctx("share_count", static_cast<std::int64_t>(shares_it->second.size()))
                            .ctx("certificate_probability", certificate.probability_sum)
                            .ctx("probability_sum", probability_sum.value())
                            .ctx("certificate_assigned", certificate.assigned_passengers)
                            .ctx("assigned_sum", passenger_sum.value())
                    );
                }
                if (!almost_equal_scalar(probability_sum.value(), 1.0)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("paper split invariant failed: probabilities over C(a) do not sum to one")
                            .ctx("origin"         , demand.origin.get())
                            .ctx("destination"    , demand.destination.get())
                            .ctx("interval_id"    , demand.interval.get())
                            .ctx("c_a_size"       , static_cast<std::int64_t>(c_a_size))
                            .ctx("probability_sum", probability_sum.value())
                    );
                }
            }

            return mathfp::kUnit;
        }

        void assign_search_alternative_count(
              AssignmentOdResult&                           od_result
            , const std::map<grouping::OdKey, std::size_t>& search_counts
            , const grouping::OdKey&                        od
        ) {
            if (const auto search_it = search_counts.find(od); search_it != search_counts.end()) {
                od_result.diagnostics.search.alternative_count = search_it->second;
            }
        }

        mathfp::Expected<ChosenConnectionIndexMap> append_chosen_connections(
              AssignmentOdResult&                         od_result
            , const PreprocessedNetwork&                  network
            , const grouping::BorrowedOdConnectionGroups& chosen_by_od
            , const grouping::OdKey&                      od
        ) {
            ChosenConnectionIndexMap chosen_connection_indices;
            const auto chosen_it = chosen_by_od.find(od);
            if (chosen_it == chosen_by_od.end()) {
                return chosen_connection_indices;
            }

            od_result.connections.reserve(chosen_it->second.size());
            for (const auto* connection : chosen_it->second) {
                MATHFP_TRY_LET(
                      AssignmentConnection
                    , mapped_connection
                    , build_assignment_connection(*connection, network)
                );
                const auto index = od_result.connections.size();
                chosen_connection_indices.emplace(grouping::connection_trace_key(*connection), index);
                od_result.connections.push_back(std::move(mapped_connection));
            }
            od_result.diagnostics.choice.chosen_alternative_count =
                od_result.connections.size();
            return chosen_connection_indices;
        }

        mathfp::Expected<AssignmentDemandInterval> build_assignment_interval(
              const InputModel&               input
            , const DemandEntry&              demand
            , const grouping::ShareGroups&    shares_by_key
            , const ChosenConnectionIndexMap& chosen_connection_indices
        ) {
            MATHFP_TRY_LET(
                  const TimeInterval*
                , interval
                , find_interval(input, demand.interval)
            );

            AssignmentDemandInterval interval_result{
                  .interval            = *interval
                , .demand_passengers   = demand.passengers
                , .assigned_passengers = 0.0
                , .shares              = {}
            };

            const auto shares_it = shares_by_key.find(grouping::demand_key(demand));
            if (shares_it == shares_by_key.end()) {
                return interval_result;
            }

            interval_result.shares.reserve(shares_it->second.size());
            mathfp::CompensatedSum<double> assigned_passengers;
            for (const auto* share : shares_it->second) {
                const auto trace_key     = grouping::connection_trace_key(share->connection);
                const auto connection_it = chosen_connection_indices.find(trace_key);
                if (connection_it == chosen_connection_indices.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("assignment output mapping could not match split share to chosen connection")
                            .ctx("origin"     , share->origin     .get())
                            .ctx("destination", share->destination.get())
                            .ctx("interval_id", share->interval   .get())
                    );
                }

                interval_result.shares.push_back(
                    AssignmentIntervalShare{
                        .connection_index = AssignmentConnectionRef{
                            static_cast<std::int64_t>(connection_it->second)
                        }
                        , .passengers      = share->passengers
                        , .probability     = share->probability
                        , .independence    = share->independence
                        , .split_impedance = share->split_impedance
                    }
                );
                assigned_passengers.add(share->passengers);
            }
            interval_result.assigned_passengers = assigned_passengers.value();

            return interval_result;
        }

        mathfp::Expected<mathfp::Unit> append_demand_intervals(
              AssignmentOdResult&                od_result
            , const InputModel&                  input
            , const grouping::DemandEntryGroups& demand_by_od
            , const grouping::ShareGroups&       shares_by_key
            , const ChosenConnectionIndexMap&    chosen_connection_indices
            , const grouping::OdKey&             od
        ) {
            const auto demand_it = demand_by_od.find(od);
            if (demand_it == demand_by_od.end()) {
                return mathfp::kUnit;
            }

            od_result.intervals.reserve(demand_it->second.size());
            mathfp::CompensatedSum<double> total_demand_passengers;
            mathfp::CompensatedSum<double> assigned_passengers;
            for (const auto* demand : demand_it->second) {
                total_demand_passengers.add(demand->passengers);

                MATHFP_TRY_LET(
                      AssignmentDemandInterval
                    , interval_result
                    , build_assignment_interval(
                          input
                        , *demand
                        , shares_by_key
                        , chosen_connection_indices
                    )
                );
                assigned_passengers.add(interval_result.assigned_passengers);
                od_result.intervals.push_back(std::move(interval_result));
            }
            od_result.total_demand_passengers = total_demand_passengers.value();
            od_result.assigned_passengers = assigned_passengers.value();

            return mathfp::kUnit;
        }

        mathfp::Expected<AssignmentOdResult> build_assignment_od_result(
              const grouping::OdKey&                        od
            , const InputModel&                             input
            , const PreprocessedNetwork&                    network
            , const std::map<grouping::OdKey, std::size_t>& search_counts
            , const grouping::BorrowedOdConnectionGroups&   chosen_by_od
            , const grouping::DemandEntryGroups&            demand_by_od
            , const grouping::ShareGroups&                  shares_by_key
            , const OdDayPaperSplitSummaryMap*               paper_split_summaries = nullptr
        ) {
            auto od_result = make_empty_od_result(od);
            assign_search_alternative_count(od_result, search_counts, od);
            if (paper_split_summaries != nullptr) {
                if (const auto it = paper_split_summaries->find(od);
                    it != paper_split_summaries->end()) {
                    od_result.paper_split = it->second;
                }
            }

            MATHFP_TRY_LET(
                  ChosenConnectionIndexMap
                , chosen_connection_indices
                , append_chosen_connections(
                      od_result
                    , network
                    , chosen_by_od
                    , od
                )
            );
            MATHFP_TRY(append_demand_intervals(
                  od_result
                , input
                , demand_by_od
                , shares_by_key
                , chosen_connection_indices
                , od
            ));

            return od_result;
        }

    }  // namespace

    mathfp::Expected<AssignmentOutput> build_assignment_output_impl(
          const InputModel&                      input
        , const PreprocessedNetwork&             network
        , const ConnectionSearchResult&          search_result
        , const ConnectionChoiceResult&          choice_result
        , const DemandSplitResult&               split_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    ) {
        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(capacity_aware));
        MATHFP_TRY(validate_choice_flat_projection(choice_result));
        MATHFP_TRY(validate_split_shares_are_task_local(choice_result, split_result));

        const auto search_counts = grouping::count_connections_by_od(search_result);
        // Output remains OD-shaped by specification. This grouping is only an
        // output projection over the task-local choice result.
        const auto chosen_by_od  = grouping::group_connection_ptrs_by_od(choice_result.connections);
        const auto demand_by_od  = grouping::group_demand_entries_by_od(input.demand);
        const auto shares_by_key = grouping::group_shares_by_demand_key(split_result.shares);
        const auto all_ods       = collect_all_ods(search_counts, chosen_by_od, demand_by_od);

        /*
         * This legacy output path is kept for non-default diagnostics and
         * compatibility. The required OD-day production path below receives
         * elementary_segment_loads from the origin-streaming pipeline.
         */
        MATHFP_TRY_LET(
              AssignmentLoads
            , loads
            , build_assignment_loads(split_result)
        );
        MATHFP_TRY(validate_loads_are_split_projection(split_result, loads));
        MATHFP_TRY_LET(
              ElementarySegmentLoads
            , elementary_segment_loads
            , build_elementary_segment_loads(split_result)
        );
        MATHFP_TRY_LET(
              AssignmentSkimMatrix
            , skim_matrix
            , build_assignment_skim_matrix(
                  choice_result
                , input
                , split_result
                , skim_config
            )
        );
        MATHFP_TRY_LET(
              ElementarySegmentOverloadAssessment
            , vehicle_journey_item_loads
            , build_elementary_segment_overload_assessment_output(
                  elementary_segment_loads
                , input.intervals
                , vehicle_journey_item_capacity
                , execution
            )
        );

        AssignmentOutput output{
              .mode        = AssignmentOutputMode::Calculated
            , .export_profile = execution.output_export_profile
            , .summary     = build_output_summary(input, search_result, choice_result, split_result)
            , .od_results  = {}
            , .loads       = std::move(loads)
            , .elementary_segment_loads = std::move(elementary_segment_loads)
            , .vehicle_journey_item_loads = std::move(vehicle_journey_item_loads)
            , .skim_matrix = std::move(skim_matrix)
            , .capacity_aware = capacity_aware
        };
        output.od_results.reserve(all_ods.size());

        for (const auto& [od, _] : all_ods) {
            MATHFP_TRY_LET(
                  AssignmentOdResult
                , od_result
                , build_assignment_od_result(
                      od
                    , input
                    , network
                    , search_counts
                    , chosen_by_od
                    , demand_by_od
                    , shares_by_key
                )
            );
            output.od_results.push_back(std::move(od_result));
        }

        output.summary.od_count = output.od_results.size();
        MATHFP_TRY(validate_output_summary_semantics(output));
        return output;
    }

    mathfp::Expected<AssignmentOutput> build_od_day_assignment_output_impl(
          const InputModel&                      input
        , const PreprocessedNetwork&             network
        , const OdDayPathSearchSummary&          search_summary
        , const OdDayPathChoiceResult&           choice_result
        , const DemandSplitResult&               split_result
        , const ElementarySegmentLoads&          elementary_segment_loads
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const AssignmentPeriodConfig&           assignment_period
        , const ConnectionAdmissibilityConfig&    admissibility_config
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    ) {
        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(capacity_aware));
        MATHFP_TRY(validate_od_day_choice_flat_projection(choice_result));
        MATHFP_TRY(validate_split_shares_are_od_day_choice_local(choice_result, split_result));
        MATHFP_TRY(validate_od_day_paper_split_runtime_contract(
              input
            , split_result
            , assignment_period
            , admissibility_config
        ));
        MATHFP_TRY(validate_elementary_segment_load_projection(
              split_result
            , elementary_segment_loads
        ));

        const auto search_counts = build_od_day_search_count_map(search_summary);
        const auto chosen_by_od  = grouping::group_connection_ptrs_by_od(choice_result);
        const auto demand_by_od  = grouping::group_demand_entries_by_od(input.demand);
        const auto shares_by_key = grouping::group_shares_by_demand_key(split_result.shares);
        const auto all_ods       = collect_all_ods(search_counts, chosen_by_od, demand_by_od);
        MATHFP_TRY_LET(
              OdDayPaperSplitSummaryMap
            , paper_split_summaries
            , build_od_day_paper_split_summary_map(
                  choice_result
                , split_result
            )
        );
        MATHFP_TRY_LET(
              AssignmentLoads
            , visum_loads
            , build_day_path_assignment_loads(split_result)
        );
        MATHFP_TRY_LET(
              AssignmentSkimMatrix
            , skim_matrix
            , build_assignment_skim_matrix(
                  choice_result
                , input
                , split_result
                , assignment_period
                , admissibility_config
                , skim_config
            )
        );
        MATHFP_TRY_LET(
              ElementarySegmentOverloadAssessment
            , elementary_overload
            , build_elementary_segment_overload_assessment_output(
                  elementary_segment_loads
                , input.intervals
                , vehicle_journey_item_capacity
                , execution
            )
        );
        MATHFP_TRY(validate_od_day_production_load_contract(
              split_result
            , elementary_segment_loads
            , visum_loads
            , elementary_overload
        ));

        AssignmentOutput output{
              .mode        = AssignmentOutputMode::Calculated
            , .export_profile = execution.output_export_profile
            , .summary     = build_od_day_output_summary(
                  input
                , search_summary
                , choice_result
                , split_result
                , paper_split_summaries
              )
            , .od_results  = {}
            , .loads       = std::move(visum_loads)
            , .elementary_segment_loads = elementary_segment_loads
            , .vehicle_journey_item_loads = std::move(elementary_overload)
            , .skim_matrix = std::move(skim_matrix)
            , .capacity_aware = capacity_aware
        };
        output.od_results.reserve(all_ods.size());

        for (const auto& [od, _] : all_ods) {
            MATHFP_TRY_LET(
                  AssignmentOdResult
                , od_result
                , build_assignment_od_result(
                      od
                    , input
                    , network
                    , search_counts
                    , chosen_by_od
                    , demand_by_od
                    , shares_by_key
                    , &paper_split_summaries
                )
            );
            output.od_results.push_back(std::move(od_result));
        }

        output.summary.od_count = output.od_results.size();
        MATHFP_TRY(validate_output_summary_semantics(output));
        return output;
    }

    mathfp::Expected<AssignmentOutput> build_timed_connection_diagnostics_output_impl(
          const InputModel&
        , const ConnectionSearchResult&          search_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    ) {
        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(capacity_aware));
        MATHFP_TRY(validate_assignment_execution_config(execution));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_input(
            vehicle_journey_item_capacity
        ));
        MATHFP_TRY(validate_skim_matrix_config(skim_config));

        const auto search_counts = grouping::count_connections_by_od(search_result);
        AssignmentOutput output{
              .mode        = AssignmentOutputMode::TimedConnectionDiagnostics
            , .export_profile = execution.output_export_profile
            , .summary     = build_timed_connection_diagnostics_output_summary(search_result)
            , .od_results  = {}
            , .loads       = AssignmentLoads{}
            , .elementary_segment_loads = ElementarySegmentLoads{}
            , .vehicle_journey_item_loads =
                  make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment()
            , .skim_matrix = AssignmentSkimMatrix{
                  .status = skim_config.enabled
                      ? AssignmentSkimMatrixStatus::SkippedAssignmentDisabled
                      : AssignmentSkimMatrixStatus::DisabledByConfig
              }
            , .capacity_aware = capacity_aware
        };
        output.od_results.reserve(search_counts.size());
        for (const auto& [od, count] : search_counts) {
            output.od_results.push_back(
                AssignmentOdResult{
                      .origin                  = od.origin
                    , .destination             = od.destination
                    , .total_demand_passengers = 0.0
                    , .assigned_passengers     = 0.0
                    , .connections             = {}
                    , .intervals               = {}
                    , .diagnostics             = AssignmentOdDiagnostics{
                          .search = AssignmentOdSearchDiagnostics{
                              .alternative_count = count
                          }
                        , .choice = AssignmentOdChoiceDiagnostics{}
                      }
                }
            );
        }

        output.summary.od_count = output.od_results.size();
        MATHFP_TRY(validate_timed_diagnostics_only_output_contract(output));
        MATHFP_TRY(validate_output_summary_semantics(output));
        return output;
    }

    mathfp::Expected<AssignmentOutput> build_assignment_disabled_output_impl(
          const InputModel&                      input
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    ) {
        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(capacity_aware));
        MATHFP_TRY(validate_assignment_execution_config(execution));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_input(
            vehicle_journey_item_capacity
        ));
        MATHFP_TRY(validate_skim_matrix_config(skim_config));
        const auto demand_by_od = grouping::group_demand_entries_by_od(input.demand);
        const grouping::ShareGroups shares_by_key{};
        const ChosenConnectionIndexMap chosen_connection_indices{};

        AssignmentOutput output{
              .mode        = AssignmentOutputMode::AssignmentDisabled
            , .export_profile = execution.output_export_profile
            , .summary     = build_disabled_output_summary(input)
            , .od_results  = {}
            , .loads       = AssignmentLoads{}
            , .elementary_segment_loads = ElementarySegmentLoads{}
            , .vehicle_journey_item_loads =
                  make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment()
            , .skim_matrix = AssignmentSkimMatrix{
                  .status = skim_config.enabled
                      ? AssignmentSkimMatrixStatus::SkippedAssignmentDisabled
                      : AssignmentSkimMatrixStatus::DisabledByConfig
              }
            , .capacity_aware = capacity_aware
        };
        output.od_results.reserve(demand_by_od.size());

        for (const auto& [od, _] : demand_by_od) {
            auto od_result = make_empty_od_result(od);
            MATHFP_TRY(append_demand_intervals(
                  od_result
                , input
                , demand_by_od
                , shares_by_key
                , chosen_connection_indices
                , od
            ));
            output.od_results.push_back(std::move(od_result));
        }

        output.summary.od_count = output.od_results.size();
        MATHFP_TRY(validate_output_summary_semantics(output));
        return output;
    }

    mathfp::Expected<AssignmentOutput> build_all_zone_search_output_impl(
          const InputModel&
        , const AllZoneConnectionSearchResult&   search_result
        , const VehicleJourneyItemCapacityInput& vehicle_journey_item_capacity
        , const AssignmentExecutionConfig&        execution
        , const SkimMatrixConfig&                skim_config
        , const CapacityAwareAssignmentDiagnostics& capacity_aware
    ) {
        MATHFP_TRY(validate_capacity_aware_assignment_diagnostics(capacity_aware));
        MATHFP_TRY(validate_assignment_execution_config(execution));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_input(
            vehicle_journey_item_capacity
        ));
        MATHFP_TRY(validate_skim_matrix_config(skim_config));

        AssignmentOutput output{
              .mode        = AssignmentOutputMode::AllZoneSearch
            , .export_profile = execution.output_export_profile
            , .summary     = build_all_zone_search_output_summary(search_result)
            , .od_results  = {}
            , .loads       = AssignmentLoads{}
            , .elementary_segment_loads = ElementarySegmentLoads{}
            , .vehicle_journey_item_loads =
                  make_skipped_assignment_disabled_vehicle_journey_item_overload_assessment()
            , .skim_matrix = AssignmentSkimMatrix{
                  .status = skim_config.enabled
                      ? AssignmentSkimMatrixStatus::SkippedAssignmentDisabled
                      : AssignmentSkimMatrixStatus::DisabledByConfig
              }
            , .capacity_aware = capacity_aware
        };
        output.od_results.reserve(output.summary.od_count);

        for (const auto& tree : search_result.tree_results) {
            for (const auto& target : tree.target_results) {
                output.od_results.push_back(
                    AssignmentOdResult{
                          .origin                  = target.origin
                        , .destination             = target.destination
                        , .total_demand_passengers = 0.0
                        , .assigned_passengers     = 0.0
                        , .connections             = {}
                        , .intervals               = {}
                        , .diagnostics             = AssignmentOdDiagnostics{
                              .search = AssignmentOdSearchDiagnostics{
                                  .alternative_count = all_zone_target_connection_count(target)
                              }
                            , .choice = AssignmentOdChoiceDiagnostics{}
                          }
                    }
                );
            }
        }

        MATHFP_TRY(validate_output_summary_semantics(output));
        return output;
    }

}  // namespace timetable::domain::assignment::detail
