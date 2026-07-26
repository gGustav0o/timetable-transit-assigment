#include "timetable/domain/assignment/search/runtime/search_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/od_day_path_contract.hpp"
#include "timetable/domain/assignment/od_day_path_runtime.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_time_domain_diagnostics.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/projection/contract.hpp"
#include "timetable/domain/assignment/search/projection/complete_connection.hpp"
#include "timetable/domain/assignment/search/projection/sink.hpp"
#include "timetable/domain/assignment/search/pruning/suffix_lower_bound.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/od_day/successor.hpp"
#include "timetable/domain/assignment/search/od_day/supply_graph.hpp"
#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"
#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search/runtime/accepted_successor_application.hpp"
#include "timetable/domain/assignment/search/runtime/batch_context.hpp"
#include "timetable/domain/assignment/search/runtime/batch_diagnostics.hpp"
#include "timetable/domain/assignment/search/runtime/batch_planning.hpp"
#include "timetable/domain/assignment/search/runtime/batch_runner.hpp"
#include "timetable/domain/assignment/search/runtime/batch_state.hpp"
#include "timetable/domain/assignment/search/runtime/batch_tree_execution.hpp"
#include "timetable/domain/assignment/search/runtime/cancellation.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/assignment/search/runtime/od_day_frontier_synchronization.hpp"
#include "timetable/domain/assignment/search/runtime/parallel.hpp"
#include "timetable/domain/assignment/search/runtime/reachability_masks.hpp"
#include "timetable/domain/assignment/search/runtime/result_materialization.hpp"
#include "timetable/domain/assignment/search/runtime/result_finalization.hpp"
#include "timetable/domain/assignment/search/runtime/root_initialization.hpp"
#include "timetable/domain/assignment/search/tree/level_expansion.hpp"
#include "timetable/domain/assignment/search/tree/tree_successor_step.hpp"
#include "timetable/domain/assignment/search/tree/tree_runner.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::runtime {
    using namespace detail;

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const BranchAndBoundSearchRequest& request
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        const auto& network                       = request.network;
        const auto  tasks                         = request.tasks;
        const auto& execution                     = request.execution;
        const auto& params                        = request.params;
        const auto& search_cost                   = request.search_cost;
        const auto& choice_config                 = request.choice_config;
        const auto& assignment_period             = request.assignment_period;
        const auto& admissibility_config          = request.admissibility_config;
        const auto& complete_connection_dominance = request.complete_connection_dominance;
        const auto  diagnostics                   = request.diagnostics;

        const auto execution_mode = execution.config.mode;
        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::DemandTasks) {
            return mathfp::unexpected(
                mathfp::invalid_arg("ConnectionSearchResult search currently supports only DemandTasks result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));

        both("search: branch-and-bound");
        log(
            fmt::format(
                "search input: route_segments = {:>8}  connection_segments = {:>8}"
                "  tasks = {:>8}  execution_mode = {}  max_transfers = {}"
                , network.route_segments     .size()
                , network.connection_segments.size()
                , tasks.size()
                , to_string(execution_mode)
                , params.transfers.max_transfers.get()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search cost: mode={}  fare_scale={:.6f}  capacity_iteration={}  capacity_index(loads/capacities/trips/prefixes)={}/{}/{}/{}"
                , to_string(search_cost.mode)
                , search_cost.fare_scale
                , diagnostics.capacity_iteration
                , search_cost.capacity.index.load_positions.size()
                , search_cost.capacity.index.capacity_positions.size()
                , search_cost.capacity.index.capacity_trip_positions.size()
                , search_cost.capacity.index.penalty_prefixes.size()
            )
            , LogLevel::Info
        );

        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , default_pruning_execution
            , plan_search_pruning_execution(
                  SearchPruningModelConfig{
                      .requested_state_space =
                          SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  }
                , SearchPruningRolloutStage::Disabled
                , params.search_tolerances
            )
        );
        const auto& effective_pruning_execution =
            request.pruning_execution.has_value()
                ? request.pruning_execution->get()
                : default_pruning_execution;

        log(
            fmt::format(
                "search setup: tasks = {:>8}  fare_scale = {:.6f}  execution_mode = {}"
                , tasks.size()
                , search_cost.fare_scale
                , to_string(execution_mode)
            )
            , LogLevel::Info
        );
        log(
            format_search_pruning_execution_summary(summarize(effective_pruning_execution))
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search projection contract: result_projection={} partial_retention_scope={} complete_retention=projection_slot_local"
                , to_string(execution.config.result_projection)
                , to_string(execution.config.partial_retention_scope)
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "timed/task contour: status=diagnostic_only formulation={} diagnostic_mode={} carrier=raw_timed_branch_trace result_projection={} demand_intervals_in_search={} od_day_production_separate=yes"
                , to_string(execution.config.formulation)
                , execution.config.diagnostic_mode ? "true" : "false"
                , to_string(execution.config.result_projection)
                , execution_mode == SearchExecutionMode::IntervalLocal
                    ? "interval_local"
                    : "origin_period_domain"
            )
            , LogLevel::Info
        );

        if (tasks.empty()) {
            status("search: no positive-demand search tasks available");
        }

        if (execution_mode == SearchExecutionMode::OriginPeriod) {
            if (search_cost.mode != SearchCostMode::BaseOnly) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search currently supports only base search cost")
                        .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
                );
            }
            if (!execution.time_domain_execution.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search requires SearchTimeDomainExecution")
                );
            }
        }
        MATHFP_TRY_LET(
              SearchBatchPlan
            , batch_plan
            , plan_search_batches(
                  tasks
                , execution
                , diagnostics
                , SearchBatchPlanningOptions{ .summarize_time_domains = true }
            )
        );
        const auto& origin_period_tree_jobs = batch_plan.tree_jobs;
        const auto& batches = batch_plan.batches;
        if (execution_mode == SearchExecutionMode::OriginPeriod) {
            log(
                fmt::format(
                    "search tree jobs: mode={} jobs = {:>8}  tasks = {:>8}"
                    , to_string(execution_mode)
                    , origin_period_tree_jobs.size()
                    , tasks.size()
                )
                , LogLevel::Info
            );
        }

        const auto residual_reverse_graph = build_residual_reverse_graph(
              network.route_segments
            , network.connection_segments
        );
        const auto& batch_execution_diagnostics =
            batch_plan.batch_diagnostics;
        const auto expected_tree_count = batch_plan.expected_tree_count;
        const auto& search_domain_summary =
            *batch_plan.time_domain_summary;
        log(
            fmt::format(
                "search batching: mode={} batches = {:>8}  tasks = {:>8}"
                , to_string(execution_mode)
                , batches.size()
                , tasks.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search diagnostics: search_execution_mode={} origin_scope={} time_domain_source={} destination_scope={} result_projection={} partial_retention_scope={} phase_invariant_validation={}"
                  " tree_count={} expected_tree_count={} tree_count_delta={} search_origin_count={} declared_zone_count={} declared_zone_request_count={}"
                  " active_demand_origin_count={} completion_target_count={} projection_slot_count={}"
                  " zero_completion_target_tree_count={} zero_projection_task_tree_count={}"
                  " max_completion_targets_per_tree={} max_projection_tasks_per_tree={}"
                  " search_domain_summary=\"{}\""
                , to_string(execution_mode)
                , to_string(execution.config.origin_scope)
                , to_string(execution.config.time_domain_source)
                , to_string(execution.config.destination_scope)
                , to_string(execution.config.result_projection)
                , to_string(execution.config.partial_retention_scope)
                , diagnostics.validate_phase_invariants ? "on" : "off"
                , batches.size()
                , expected_tree_count
                , signed_count_delta(batches.size(), expected_tree_count)
                , search_origin_count(batches)
                , diagnostics.declared_zone_count
                , execution.declared_zones.size()
                , active_demand_origin_count(tasks)
                , batch_execution_diagnostics.completion_target_count
                , batch_execution_diagnostics.projection_task_count
                , batch_execution_diagnostics.zero_completion_target_tree_count
                , batch_execution_diagnostics.zero_projection_task_tree_count
                , batch_execution_diagnostics.max_completion_targets_per_tree
                , batch_execution_diagnostics.max_projection_tasks_per_tree
                , format_search_time_domain_summary(search_domain_summary)
            )
            , LogLevel::Info
        );

        std::vector<SearchSlotResult> slot_results;
        for (std::size_t i = 0; i < batches.size(); ++i) {
            const auto& batch = batches[i];
            const auto total_found = search_slot_connection_count(slot_results);
            if (i == 0 || (i % kTaskProgressStep) == 0 || (i + 1) == batches.size()) {
                status(
                    fmt::format(
                          "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} total_found={}"
                        , i + 1
                        , batches.size()
                        , batch.key.origin.get()
                        , format_batch_interval(batch.key.interval)
                        , batch.projection_slots.size()
                        , batch.completion_targets.size()
                        , diagnostics.capacity_iteration
                        , total_found
                    )
                );
            }
            log(
                fmt::format(
                      "search batch start: {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} cumulative_found={}"
                    , i + 1
                    , batches.size()
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                    , batch.projection_slots.size()
                    , batch.completion_targets.size()
                    , diagnostics.capacity_iteration
                    , total_found
                )
                , LogLevel::Info
            );
            MATHFP_TRY_LET(
                  std::vector<SearchSlotResult>
                , batch_slot_results
                , search_batch_connections(
                      batch
                    , network
                    , residual_reverse_graph
                    , params
                    , search_cost
                    , choice_config
                    , assignment_period
                    , admissibility_config
                    , effective_pruning_execution
                    , complete_connection_dominance
                    , execution.config.partial_retention_scope
                    , diagnostics
                    , i
                    , batches.size()
                )
            );
            slot_results.insert(
                  slot_results.end()
                , std::make_move_iterator(batch_slot_results.begin())
                , std::make_move_iterator(batch_slot_results.end())
            );
        }

        auto result = materialize_demand_task_search_result(
              tasks
            , std::move(slot_results)
        );
        log(
            fmt::format(
                  "search result: tasks = {:>8}  connections = {:>8}"
                , result.task_results.size()
                , search_connection_count(result)
            )
            , LogLevel::Info
        );
        both("search: branch-and-bound done");
        return result;
    }

}  // namespace timetable::domain::assignment::runtime
