#include "timetable/domain/assignment/od_day_path_runtime.hpp"

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
#include "timetable/domain/assignment/search/tree/paper_successor_step.hpp"
#include "timetable/domain/assignment/search/tree/tree_runner.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::runtime {
    using namespace detail;

    mathfp::Expected<mathfp::Unit> search_od_day_paths_by_origin_branch_and_bound(
        OdDayPathOriginSearchRequest request
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        auto        origin_sink                   = std::move(request.origin_sink);
        const auto& search_request                = request.search;
        const auto& network                       = search_request.network;
        const auto  tasks                         = search_request.tasks;
        const auto& execution                     = search_request.execution;
        const auto& params                        = search_request.params;
        const auto& search_cost                   = search_request.search_cost;
        const auto& choice_config                 = search_request.choice_config;
        const auto& assignment_period             = search_request.assignment_period;
        const auto& admissibility_config          = search_request.admissibility_config;
        const auto& complete_connection_dominance =
            search_request.complete_connection_dominance;
        const auto  diagnostics                   = search_request.diagnostics;

        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::OdDayPairs) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OdDayPathSearchResult search requires OdDayPairs result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        if (execution.config.mode != SearchExecutionMode::OriginPeriod) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search requires origin-period execution")
                    .ctx("execution_mode", std::string(to_string(execution.config.mode)))
            );
        }
        if (search_cost.mode != SearchCostMode::BaseOnly) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search currently supports only base search cost")
                    .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
            );
        }
        if (!execution.time_domain_execution.has_value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day origin-period search requires SearchTimeDomainExecution")
            );
        }
        if (!origin_sink) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day by-origin search requires a result sink")
            );
        }
        if (execution.config.partial_retention_scope != SearchPartialRetentionScope::TreeGlobal) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search requires tree_global partial retention")
                    .ctx("partial_retention_scope", std::string(to_string(execution.config.partial_retention_scope)))
            );
        }

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));
        const auto od_day_contract = make_od_day_path_search_contract(
            diagnostics.declared_zone_count
        );
        if (!satisfies_od_day_path_search_contract(od_day_contract)) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day search production contract is not satisfied")
                    .ctx(
                          "declared_origin_count"
                        , static_cast<std::int64_t>(od_day_contract.declared_origin_count)
                    )
            );
        }

        both("search: OD-day branch-and-bound");
        const auto day_path_retention_config = DayPathRetentionConfig{};
        const auto day_path_alternative_limit =
            day_path_retention_config.max_alternatives_per_od.has_value()
                ? std::to_string(*day_path_retention_config.max_alternatives_per_od)
                : std::string("unbounded");
        const auto day_path_support_limit =
            day_path_retention_config.max_supports_per_path.has_value()
                ? std::to_string(*day_path_retention_config.max_supports_per_path)
                : std::string("unbounded");
        log(
            fmt::format(
                  "OD-day projection contract: paper=connection_tree partial_retention_scope={} tree_label=network_node_c_y c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink support=connection_segment_witness dominance=dep_arr_imp_nt tolerance=node_local production_carrier=compact_connection_segment_prefix path_identity=compact_prefix supply_graph=preprocessed_connection_segment_index frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_contract=single_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available late_guard=assert_only transfer_walk=first_class_segment composite_transfer_walk=disabled_in_production label_representatives=unbounded tree_bounds=c_y_before_day_path_sink suffix_bound=disabled_for_od_day completed_connection_projection=immediate_day_path_sink od_alternative_retention=production_slots_only signature=route_stop_line_pattern day_path_retention_policy={} max_alternatives_per_od={} max_supports_per_path={} computation_contract={}"
                , to_string(execution.config.partial_retention_scope)
                , day_path_retention_limit_policy_name(
                      day_path_retention_config.limit_policy
                  )
                , day_path_alternative_limit
                , day_path_support_limit
                , to_log_token(OdDaySearchComputationContract::PaperConnectionSegmentTree)
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
            search_request.pruning_execution.has_value()
                ? search_request.pruning_execution->get()
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

        /*
         * The paper OD-day production contour does not use suffix reachability
         * masks as a branch filter. Keep an empty graph only to satisfy the
         * shared batch-search signature; timed/diagnostic contours still build
         * and use residual reachability in their own entry points.
         */
        const ResidualReverseGraph residual_reverse_graph{};
        const auto& batch_execution_diagnostics =
            batch_plan.batch_diagnostics;
        const auto expected_tree_count = batch_plan.expected_tree_count;
        if (tree_jobs.size() != expected_tree_count) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day search must build exactly one tree per declared origin")
                    .ctx("tree_count", static_cast<std::int64_t>(tree_jobs.size()))
                    .ctx("expected_tree_count", static_cast<std::int64_t>(expected_tree_count))
                    .ctx("declared_zone_count", static_cast<std::int64_t>(diagnostics.declared_zone_count))
                    .ctx("declared_zone_request_count", static_cast<std::int64_t>(execution.declared_zones.size()))
            );
        }
        if (execution.config.origin_scope == SearchOriginScope::DeclaredZones
            && tree_jobs.size() != execution.declared_zones.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day declared-origin tree count disagrees with declared zone support")
                    .ctx("tree_count", static_cast<std::int64_t>(tree_jobs.size()))
                    .ctx("declared_zone_request_count", static_cast<std::int64_t>(execution.declared_zones.size()))
            );
        }
        MATHFP_TRY(validate_od_day_production_batches(
              batches
            , expected_tree_count
            , execution.config.destination_scope
            , execution.config.destination_scope == SearchDestinationScope::DeclaredZones
                ? execution.declared_zones.size()
                : batch_execution_diagnostics.max_completion_targets_per_tree
        ));
        log(
            fmt::format(
                  "OD-day computational profile: contour=paper_branch_and_bound production_carrier=compact_connection_segment_prefix branch_projection_state=none reachability_prefilter=disabled_not_built reachability_masks=disabled supply_graph=preprocessed_connection_segment_index frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_generation=single_paper_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available transfer_walk_successor=first_class_connection_segment composite_transfer_walk=disabled_in_production walk_successor_lookup=lazy_phase_specific walk_indices=access_transfer_egress tree_label_scope=network_node_c_y c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink dominance=dep_arr_imp_nt tolerance=node_local path_identity=compact_prefix od_signature=route_stop_line_pattern structural_day_contour=diagnostics_only trees={} destinations={} time_horizon=service_day result=post_layer_day_path_support_sets split_contract=paper_connection_split split_interval_admissibility=all_interval_admissible_timed_supports single_best_support=disabled split_load=lazy_support_envelope primary_load=elementary_segment_loads max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={}"
                , tree_jobs.size()
                , execution.config.destination_scope == SearchDestinationScope::DeclaredZones
                    ? execution.declared_zones.size()
                    : batch_execution_diagnostics.max_completion_targets_per_tree
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day paper connection segment tree: connection_segments={} route_segments={} timed_buckets={} boarding_stop_buckets={} access_walks={} transfer_walks={} egress_walks={}"
                , network.connection_segments.size()
                , network.route_segments.size()
                , network.connection_index.timed_buckets.size()
                , network.connection_index.boarding_stop_buckets.size()
                , network.connection_index.access_walk_order.size()
                , network.connection_index.transfer_walk_order.size()
                , network.connection_index.egress_walk_order.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day search diagnostics: paper_clauses=successor_then_c_y_then_sink c_y_scope=node_local destination_in_expansion=no demand_intervals_in_search=no raw_complete_retention=no trees={} expected_trees={} tree_count_delta={} batches={} targets={} projection_slots={} partial_retention_scope={} phase_invariant_validation={} result_sink=origin"
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
        log(
            fmt::format(
                  "OD-day paper compliance: paper_reference=connection_segment_tree one_tree_per_origin={} trees={} expected_trees={} service_period=day demand_intervals_in_tree=no destination_in_expansion=no successor_order=lookup_then_feasibility_then_c_y_then_enqueue c_y_scope=node_local c_y_key=physical_y c_y_applies_to=all_connection_segments day_path_layer=post_layer production_result=day_path_support_sets raw_completed_connections_retained=no"
                , tree_jobs.size() == expected_tree_count ? "true" : "false"
                , tree_jobs.size()
                , expected_tree_count
            )
            , LogLevel::Info
        );

        const auto worker_count = search_batch_worker_count(
              batches.size()
            , execution.config
        );
        log(
            fmt::format(
                  "OD-day search parallel execution: workers={} batches={} max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={} label_representatives=unbounded merge=origin_sink_associative fast_fail=enabled"
                , worker_count
                , batches.size()
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );

        std::atomic<std::size_t> pair_count{ 0u };
        std::atomic<std::size_t> day_path_alternative_count{ 0u };
        std::atomic<std::size_t> empty_pair_count{ 0u };
        SearchParallelBatchRuntime parallel_runtime;
        std::mutex origin_sink_mutex;
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
                                "OD-day search: worker={} origin batch {}/{} origin={} targets={} projection_slots={} completed={} total_found={}"
                              , worker
                              , i + 1
                              , batches.size()
                              , batch.key.origin.get()
                              , batch.completion_targets.size()
                              , batch.projection_slots.size()
                              , completed_search_batches(parallel_runtime)
                              , day_path_alternative_count.load(std::memory_order_relaxed)
                          )
                      );
                  }

                  auto slot_results_result = search_batch_connections(
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
                  if (!slot_results_result) {
                      return mathfp::unexpected(
                          std::move(slot_results_result.error())
                      );
                  }
                  if (search_cancelled(&parallel_runtime.cancellation)) {
                      return SearchParallelBatchStepStatus::Cancelled;
                  }
                  auto slot_results = std::move(*slot_results_result);
                  auto origin_result = materialize_origin_day_search_result(
                        batch.key.origin
                      , std::move(slot_results)
                  );
                  auto origin_alternatives = std::size_t{ 0u };
                  auto origin_empty_pairs = std::size_t{ 0u };
                  for (const auto& pair_result : origin_result.pair_results) {
                      origin_alternatives += pair_result.alternatives.size();
                      if (pair_result.alternatives.empty()) {
                          ++origin_empty_pairs;
                      }
                  }
                  day_path_alternative_count.fetch_add(
                        origin_alternatives
                      , std::memory_order_relaxed
                  );
                  empty_pair_count.fetch_add(
                        origin_empty_pairs
                      , std::memory_order_relaxed
                  );
                  pair_count.fetch_add(
                        origin_result.pair_results.size()
                      , std::memory_order_relaxed
                  );
                  {
                      std::scoped_lock lock(origin_sink_mutex);
                      auto sink_result = origin_sink(std::move(origin_result));
                      if (!sink_result) {
                          return mathfp::unexpected(
                              std::move(sink_result.error())
                          );
                      }
                  }
                  return SearchParallelBatchStepStatus::Completed;
              }
        );
        if (cancelled_search_batches(parallel_runtime) != 0u) {
            log(
                fmt::format(
                      "OD-day search fast-fail cancellation: cancelled_batches={}"
                    , cancelled_search_batches(parallel_runtime)
                )
                , LogLevel::Warning
            );
        }
        MATHFP_TRY(std::move(first_worker_error));
        log(
            fmt::format(
                  "OD-day by-origin search result: carrier=paper_connection_tree result=day_path_post_layer origins={:>8} pairs={:>8} empty_pairs={:>8} expected_trees={:>8} day_path_alternatives={:>8} raw_complete_connections={:>8}"
                , batches.size()
                , pair_count.load(std::memory_order_relaxed)
                , empty_pair_count.load(std::memory_order_relaxed)
                , expected_tree_count
                , day_path_alternative_count.load(std::memory_order_relaxed)
                , std::size_t{ 0u }
            )
            , LogLevel::Info
        );
        both("search: OD-day branch-and-bound done");
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::runtime
