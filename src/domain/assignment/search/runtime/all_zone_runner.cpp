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

    mathfp::Expected<AllZoneConnectionSearchResult> search_all_zone_connections_branch_and_bound(
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

        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::CompletionTargets) {
            return mathfp::unexpected(
                mathfp::invalid_arg("AllZoneConnectionSearchResult search requires CompletionTargets result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        if (execution.config.mode != SearchExecutionMode::OriginPeriod) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone search requires origin-period execution")
                    .ctx("execution_mode", std::string(to_string(execution.config.mode)))
            );
        }
        if (search_cost.mode != SearchCostMode::BaseOnly) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone completion-target search currently supports only base search cost")
                    .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
            );
        }
        if (!execution.time_domain_execution.has_value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone origin-period search requires SearchTimeDomainExecution")
            );
        }

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));

        both("search: all-zone branch-and-bound");
        log(
            fmt::format(
                  "all-zone projection contract: partial_retention_scope={} complete_retention=completion_target_slot"
                , to_string(execution.config.partial_retention_scope)
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

        MATHFP_TRY_LET(
              SearchBatchPlan
            , batch_plan
            , plan_search_batches(
                  tasks
                , execution
                , diagnostics
            )
        );
        const auto& tree_jobs = batch_plan.tree_jobs;
        const auto& batches = batch_plan.batches;

        const auto residual_reverse_graph = build_residual_reverse_graph(
              network.route_segments
            , network.connection_segments
        );
        const auto& batch_execution_diagnostics =
            batch_plan.batch_diagnostics;
        const auto expected_tree_count = batch_plan.expected_tree_count;
        log(
            fmt::format(
                  "all-zone search comparison diagnostics: trees={} expected_trees={} tree_count_delta={} batches={} targets={} projection_slots={} partial_retention_scope={} phase_invariant_validation={} result_sink=count_only"
                , tree_jobs.size()
                , expected_tree_count
                , signed_count_delta(tree_jobs.size(), expected_tree_count)
                , batches.size()
                , batch_execution_diagnostics.completion_target_count
                , batch_execution_diagnostics.projection_task_count
                , to_string(execution.config.partial_retention_scope)
                , diagnostics.validate_phase_invariants ? "on" : "off"
            )
            , LogLevel::Info
        );

        const auto worker_count = search_batch_worker_count(
              batches.size()
            , execution.config
        );
        log(
            fmt::format(
                  "all-zone search parallel execution: workers={} batches={} max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={} fast_fail=enabled"
                , worker_count
                , batches.size()
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );

        std::atomic<std::size_t> found_connections{ 0u };
        SearchParallelBatchRuntime parallel_runtime;
        CountOnlyAllZoneSearchResultSink result_sink;
        auto first_worker_error = run_search_parallel_batches(
              parallel_runtime
            , worker_count
            , batches.size()
            , [&](std::size_t worker, std::size_t i)
                  -> mathfp::Expected<SearchParallelBatchStepStatus> {
                  const auto& batch = batches[i];
                  if (
                         i == 0
                      || (i % kTaskProgressStep) == 0
                      || (i + 1) == batches.size()
                  ) {
                      status(
                          fmt::format(
                                "all-zone search: worker={} batch {}/{} origin={} targets={} projection_slots={} completed={} total_found={}"
                              , worker
                              , i + 1
                              , batches.size()
                              , batch.key.origin.get()
                              , batch.completion_targets.size()
                              , batch.projection_slots.size()
                              , completed_search_batches(parallel_runtime)
                              , found_connections.load(std::memory_order_relaxed)
                          )
                      );
                  }

                  auto results_result = search_batch_connections(
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
                          , nullptr
                          , &parallel_runtime.cancellation
                      );
                  if (!results_result) {
                      return mathfp::unexpected(std::move(results_result.error()));
                  }
                  if (search_cancelled(&parallel_runtime.cancellation)) {
                      return SearchParallelBatchStepStatus::Cancelled;
                  }
                  auto results = std::move(*results_result);
                  found_connections.fetch_add(
                        search_slot_connection_count(results)
                      , std::memory_order_relaxed
                  );
                  auto sink_result = result_sink.accept(std::move(results));
                  if (!sink_result) {
                      return mathfp::unexpected(std::move(sink_result.error()));
                  }
                  return SearchParallelBatchStepStatus::Completed;
              }
        );
        if (cancelled_search_batches(parallel_runtime) != 0u) {
            log(
                fmt::format(
                      "all-zone search fast-fail cancellation: cancelled_batches={}"
                    , cancelled_search_batches(parallel_runtime)
                )
                , LogLevel::Warning
            );
        }
        MATHFP_TRY(std::move(first_worker_error));

        auto result = result_sink.materialize();
        log(
            fmt::format(
                  "all-zone search result: trees = {:>8}  expected_trees = {:>8}  connections = {:>8}"
                , result.tree_results.size()
                , expected_tree_count
                , search_connection_count(result)
            )
            , LogLevel::Info
        );
        both("search: all-zone branch-and-bound done");
        return result;
    }

}  // namespace timetable::domain::assignment::runtime
