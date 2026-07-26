#include "pipeline_steps.hpp"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"
#include "grouping.hpp"

namespace timetable::domain::assignment::detail {
    namespace {

        struct OdDayPipelineAccumulation final {
            OdDayPathSearchSummary             search_summary{};
            OdDayPathChoiceResult              choice{};
            DemandSplitResult                  split{};
            ElementarySegmentLoadAccumulator   elementary_segment_loads{};
            std::map<grouping::ConnectionTraceKey, std::size_t> chosen_trace_index{};
        };

        void append_od_day_search_summary(
              OdDayPathSearchSummary&       summary
            , const OriginDaySearchResult&   origin_result
        ) {
            for (const auto& pair_result : origin_result.pair_results) {
                summary.pair_counts.push_back(
                    OdDayPairConnectionCount{
                          .origin           = pair_result.origin
                        , .destination      = pair_result.destination
                        , .connection_count = pair_result.alternatives.size()
                    }
                );
            }
        }

        void append_unique_od_day_choice_connections(
              OdDayPipelineAccumulation&      accumulation
            , const OriginDayPathChoiceResult& origin_choice
        ) {
            for (const auto& pair_result : origin_choice.pair_results) {
                for (const auto& connection : pair_result.connections) {
                    const auto trace = grouping::connection_trace_key(connection);
                    if (accumulation.chosen_trace_index.emplace(
                          trace
                        , accumulation.choice.connections.size()
                    ).second) {
                        accumulation.choice.connections.push_back(connection);
                    }
                }
            }
        }

        void append_origin_day_choice_result(
              OdDayPipelineAccumulation& accumulation
            , OriginDayPathChoiceResult   origin_choice
        ) {
            append_unique_od_day_choice_connections(accumulation, origin_choice);
            for (auto& pair_result : origin_choice.pair_results) {
                std::vector<DayPathAlternative>{}.swap(pair_result.alternatives);
            }
            accumulation.choice.origin_results.push_back(std::move(origin_choice));
        }

        void append_origin_day_split_result(
              DemandSplitResult& target
            , DemandSplitResult  origin_split
        ) {
            target.shares.insert(
                  target.shares.end()
                , std::make_move_iterator(origin_split.shares.begin())
                , std::make_move_iterator(origin_split.shares.end())
            );
            target.unassigned.insert(
                  target.unassigned.end()
                , std::make_move_iterator(origin_split.unassigned.begin())
                , std::make_move_iterator(origin_split.unassigned.end())
            );
            target.od_day_split_certificates.insert(
                  target.od_day_split_certificates.end()
                , std::make_move_iterator(origin_split.od_day_split_certificates.begin())
                , std::make_move_iterator(origin_split.od_day_split_certificates.end())
            );
        }

    }  // namespace

    mathfp::Expected<AssignmentPipelineOdDayCalculatedResult>
    run_od_day_assignment_layer(
          AssignmentInput      input
        , PreprocessedNetwork  network
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        if (capacity_aware_search_enabled(input) || capacity_aware_split_enabled(input)) {
            return mathfp::unexpected(
                mathfp::invalid_arg(
                    "OD-day assignment currently supports exogenous overload assessment, not capacity-aware fixed-point assignment"
                )
            );
        }
        MATHFP_TRY(validate_required_od_day_search_execution(input.search_execution));

        both("assignment: OD-day origin streaming");
        MATHFP_TRY_LET(
              PreparedSearchStep
            , prepared
            , prepare_validated_search_step(
                  network
                , input
                , VehicleJourneyItemLoadState{}
                , SearchDiagnosticsContext{}
            )
        );
        const auto execution_request = make_search_execution_request(input, prepared);
        OdDayPipelineAccumulation accumulation{};
        const auto admissibility_config = ConnectionAdmissibilityConfig{
              .deletion    = input.connection_deletion
            , .demand_time = input.demand_segment_time
        };

        MATHFP_TRY(search_od_day_paths_by_origin_branch_and_bound(
            OdDayPathOriginSearchRequest{
                  .search = BranchAndBoundSearchRequest{
                        .network              = network
                      , .tasks                = prepared.tasks
                      , .execution            = execution_request
                      , .params               = input.params
                      , .search_cost          = prepared.search_cost
                      , .choice_config        = input.choice
                      , .assignment_period    = input.assignment_period
                      , .admissibility_config = admissibility_config
                      , .pruning_execution    = std::cref(prepared.pruning_execution)
                      , .complete_connection_dominance =
                          input.complete_connection_dominance
                      , .diagnostics          = prepared.diagnostics
                  }
                , .origin_sink =
                    [&](OriginDaySearchResult origin_result) -> mathfp::Expected<mathfp::Unit> {
                        append_od_day_search_summary(accumulation.search_summary, origin_result);
                        MATHFP_TRY_LET(
                              OriginDayDemandLoadResult
                            , origin_load
                            , load_origin_day_path_demand(
                                  origin_result
                                , input.input
                                , input.params
                                , prepared.search_cost
                                , input.choice
                                , input.demand_segment_time
                                , input.assignment_period
                                , admissibility_config
                            )
                        );
                        MATHFP_TRY(validate_od_day_origin_load_result(origin_load));
                        append_origin_day_choice_result(
                              accumulation
                            , std::move(origin_load.alternatives)
                        );
                        append_origin_day_split_result(
                              accumulation.split
                            , std::move(origin_load.split_result)
                        );
                        MATHFP_TRY(accumulate_elementary_segment_loads(
                              accumulation.elementary_segment_loads
                            , origin_load.elementary_segment_loads
                        ));
                        return mathfp::kUnit;
                    }
            }
        ));
        MATHFP_TRY_LET(
              ElementarySegmentLoads
            , elementary_segment_loads
            , materialize_elementary_segment_loads(
                  accumulation.elementary_segment_loads
            )
        );
        MATHFP_TRY(validate_od_day_primary_load_contour(
              accumulation.split
            , elementary_segment_loads
        ));

        log(
            fmt::format(
                  "OD-day load contour: production=elementary_segment_loads source=day_path split_contract=connection_split support_selection=all_interval_admissible single_best_support=disabled demand_shares={:>8} unassigned_demand={:>8} elementary_loads={:>8} secondary_visum_aggregates=route_stop_totals overload_source=elementary_segment_loads"
                , accumulation.split.shares.size()
                , accumulation.split.unassigned.size()
                , elementary_segment_loads.items.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day production output contract: output_profile={} full_path_dump={} primary_output=elementary_segment_loads route_aggregate=visum_secondary stop_aggregate=visum_secondary overload_source=elementary_segment_loads skim_summary={} share_summary_count={} share_summary_scope=metadata_od_summary_json"
                , to_string(input.execution.output_export_profile)
                , input.execution.output_export_profile
                    == AssignmentOutputExportProfile::DiagnosticFullPath
                    ? "enabled_diagnostic"
                    : "disabled_by_default"
                , input.skim_matrix.enabled ? "enabled" : "disabled"
                , accumulation.split.shares.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day assignment result: od_pairs = {:>8}  search_connections = {:>8}  structural_day_paths = {:>8}  demand_shares = {:>8}  unassigned_demand = {:>8}  elementary_loads = {:>8}"
                , accumulation.search_summary.pair_counts.size()
                , search_connection_count(accumulation.search_summary)
                , accumulation.choice.connections.size()
                , accumulation.split.shares.size()
                , accumulation.split.unassigned.size()
                , elementary_segment_loads.items.size()
            )
            , LogLevel::Info
        );
        both("assignment: OD-day origin streaming done");

        return AssignmentPipelineOdDayCalculatedResult{
              .input                         = std::move(input.input)
            , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
            , .network                       = std::move(network)
            , .search                        = std::move(accumulation.search_summary)
            , .choice                        = std::move(accumulation.choice)
            , .split                         = std::move(accumulation.split)
            , .elementary_segment_loads      = std::move(elementary_segment_loads)
            , .execution                     = input.execution
            , .assignment_period             = input.assignment_period
            , .admissibility_config          = admissibility_config
            , .skim_config                   = input.skim_matrix
            , .capacity_aware                =
                  make_capacity_aware_assignment_disabled_diagnostics(
                      input.capacity_aware_assignment.penalty_policy
                  )
        };
    }


}  // namespace timetable::domain::assignment::detail
