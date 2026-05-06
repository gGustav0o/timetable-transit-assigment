#pragma once

#include <algorithm>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/complete_connection_diagnostics.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/pipeline.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain_plan.hpp"
#include "timetable/domain/assignment/split.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail {

    struct SearchStepResult final {
        ConnectionSearchResult                    result{};
        std::optional<AllZoneConnectionSearchResult> all_zone_result{};
        double                                    fare_scale{};
        SearchCostContext                         search_cost{};
    };

    struct SplitStepResult final {
        DemandSplitResult                  result{};
        CapacityAwareAssignmentDiagnostics capacity_aware{};
    };

    inline bool capacity_aware_search_enabled(
        const AssignmentInput& input
    ) noexcept {
        return input.capacity_aware_assignment.search_mode
            == CapacityAwareSearchMode::Enabled;
    }

    inline bool capacity_aware_split_enabled(
        const AssignmentInput& input
    ) noexcept {
        return input.capacity_aware_assignment.capacity_aware_split_enabled
            && mathfp::units::as_dimless(
                input.params.split.perceived_journey_time.volume_capacity_ratio
            ) > 0.0;
    }

    inline Dimless capacity_aware_assignment_used_factor(
        const AssignmentInput& input
    ) noexcept {
        return Dimless{
            std::max(
                  mathfp::units::as_dimless(input.params.impedance.volume_capacity_ratio)
                , mathfp::units::as_dimless(
                      input.params.split.perceived_journey_time.volume_capacity_ratio
                  )
            )
        };
    }

    [[nodiscard]] inline SearchExecutionMode pipeline_search_execution_mode(
        const AssignmentInput& input
    ) noexcept {
        return input.search_execution.mode;
    }

    inline mathfp::Expected<std::optional<SearchTimeDomainExecution>>
    prepare_pipeline_search_time_domain_execution(
          const AssignmentInput& input
        , SearchExecutionMode    execution_mode
    ) {
        if (execution_mode != SearchExecutionMode::OriginPeriod) {
            return std::optional<SearchTimeDomainExecution>{};
        }

        if (input.search_execution.time_domain_source
            != SearchTimeDomainSource::DemandInduced) {
            MATHFP_TRY_LET(
                  SearchTimeDomainExecution
                , execution
                , prepare_full_period_search_time_domain_execution(
                      input.input
                    , input.search_execution.time_domain_source
                    , input.assignment_period
                )
            );
            return std::optional<SearchTimeDomainExecution>{ std::move(execution) };
        }

        return prepare_search_time_domain_execution(
              input.input
            , input.search_time_domain.model.padding_policy
            , input.params.split
            , input.search_time_domain.runtime.architecture
            , input.search_time_domain.runtime.rollout_stage
            , input.search_time_domain.model.requested_mode
        );
    }

    inline mathfp::Expected<PreprocessedNetwork> build_preprocessed_step(
        AssignmentInput& input
    ) {
        if (input.presegmented) {
            return build_preprocessed_network_from_segments(
                  std::move(input.presegmented->route_segments)
                , std::move(input.presegmented->connection_segments)
            );
        }
        return build_preprocessed_network(input.input, input.params.preprocess);
    }

    inline mathfp::Expected<PreprocessedNetwork> run_validated_preprocessing_step(
        AssignmentInput& input
    ) {
        MATHFP_TRY(validate_preprocessing_step_input(input));
        return build_preprocessed_step(input);
    }

    inline mathfp::Expected<SearchStepResult> run_validated_search_step(
          const PreprocessedNetwork& net
        , const AssignmentInput&     input
        , const VehicleJourneyItemLoadState& fixed_load_state
        , SearchDiagnosticsContext diagnostics = {}
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        const auto& params = input.params;
        MATHFP_TRY(validate_preprocessing_step_output(net, params));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            input.complete_connection_dominance
        ));
        const auto fare_scale = compute_fare_scale(
              net.connection_segments
            , params.impedance.fare_normalization
        );
        SearchCostContext search_cost{};
        if (capacity_aware_search_enabled(input)) {
            MATHFP_TRY_LET(
                  SearchCostContext
                , capacity_search_cost
                , make_capacity_aware_search_cost_context(
                      params.impedance
                    , fare_scale
                    , input.capacity_aware_assignment.penalty_policy
                    , fixed_load_state
                    , input.vehicle_journey_item_capacity.capacities
                )
            );
            search_cost = std::move(capacity_search_cost);
        } else {
            MATHFP_TRY_LET(
                  SearchCostContext
                , base_search_cost
                , make_base_search_cost_context(params.impedance, fare_scale)
            );
            search_cost = std::move(base_search_cost);
        }
        log(
            format_complete_connection_dominance_config_summary(
                summarize(input.complete_connection_dominance)
            )
            , LogLevel::Info
        );
        MATHFP_TRY(validate_search_pruning_config(input.search_pruning));
        log(
            format_search_pruning_config_summary(summarize(input.search_pruning))
            , LogLevel::Info
        );
        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , search_pruning_execution
            , plan_search_pruning_execution(
                  input.search_pruning.model
                , input.search_pruning.runtime.rollout_stage
                , params.search_tolerances
            )
        );
        MATHFP_TRY(validate_search_pruning_execution_plan(
              search_pruning_execution
            , params.search_tolerances
        ));
        log(
            format_search_pruning_execution_summary(summarize(search_pruning_execution))
            , LogLevel::Info
        );
        MATHFP_TRY(validate_search_task_builder_input(
            input.input
        ));
        MATHFP_TRY_LET(
              std::vector<SearchTask>
            , search_tasks
            , build_search_tasks(
                input.input
            )
        );
        const auto search_execution_mode = pipeline_search_execution_mode(input);
        MATHFP_TRY_LET(
              std::optional<SearchTimeDomainExecution>
            , search_time_domain_execution
            , prepare_pipeline_search_time_domain_execution(
                  input
                , search_execution_mode
            )
        );
        SearchDiagnosticsContext search_diagnostics = diagnostics;
        search_diagnostics.declared_zone_count = input.input.zones.size();
        const auto execution_request = SearchExecutionRequest{
              .config = input.search_execution
            , .time_domain_execution = search_time_domain_execution.has_value()
                ? &*search_time_domain_execution
                : nullptr
            , .declared_zones = std::span<const Zone>{
                  input.input.zones.data()
                , input.input.zones.size()
              }
        };

        if (input.search_execution.result_projection
            == SearchResultProjection::CompletionTargets) {
            MATHFP_TRY_LET(
                  AllZoneConnectionSearchResult
                , all_zone_search_result
                , search_all_zone_connections_branch_and_bound(
                      net
                    , search_tasks
                    , execution_request
                    , params
                    , search_cost
                    , input.choice
                    , input.assignment_period
                    , ConnectionAdmissibilityConfig{
                          .deletion    = input.connection_deletion
                        , .demand_time = input.demand_segment_time
                      }
                    , &search_pruning_execution
                    , input.complete_connection_dominance
                    , search_diagnostics
                )
            );
            return SearchStepResult{
                  .result          = {}
                , .all_zone_result = std::move(all_zone_search_result)
                , .fare_scale      = fare_scale
                , .search_cost     = std::move(search_cost)
            };
        }

        MATHFP_TRY_LET(
              ConnectionSearchResult
            , search_result
            , search_connections_branch_and_bound(
                  net
                , search_tasks
                , execution_request
                , params
                , search_cost
                , input.choice
                , input.assignment_period
                , ConnectionAdmissibilityConfig{
                      .deletion    = input.connection_deletion
                    , .demand_time = input.demand_segment_time
                }
                , &search_pruning_execution
                , input.complete_connection_dominance
                , search_diagnostics
            )
        );
        MATHFP_TRY(validate_search_step_output(
              search_result
            , net
            , fare_scale
            , params
            , input.assignment_period
            , ConnectionAdmissibilityConfig{
                  .deletion    = input.connection_deletion
                , .demand_time = input.demand_segment_time
              }
        ));
        return SearchStepResult{
              .result     = std::move(search_result)
            , .fare_scale = fare_scale
            , .search_cost = std::move(search_cost)
        };
    }

    inline mathfp::Expected<ConnectionChoiceResult> run_validated_choice_step(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , const SearchCostContext&      search_cost
        , const ChoiceConfig&           config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        MATHFP_TRY_LET(
              ConnectionChoiceResult
            , choice_result
            , choose_connections(
                  search_result
                , params
                , search_cost
                , config
                , assignment_period
                , admissibility_config
            )
        );
        MATHFP_TRY(validate_choice_step_output(
              choice_result
            , search_result
            , assignment_period
            , admissibility_config
        ));
        return choice_result;
    }

    inline mathfp::Expected<SplitStepResult> run_validated_split_step(
          const ConnectionChoiceResult& choice_result
        , const AssignmentInput&        input
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        const auto capacity_factor = mathfp::units::as_dimless(
            input.params.split.perceived_journey_time.volume_capacity_ratio
        );
        const auto capacity_aware_enabled =
               input.capacity_aware_assignment.capacity_aware_split_enabled
            && capacity_factor > 0.0;

        DemandSplitResult split_result{};

        if (capacity_aware_enabled) {
            MATHFP_TRY(validate_capacity_aware_split_step_input(
                  choice_result
                , input.input
                , input.params
                , input.demand_segment_time
                , input.capacity_aware_assignment
                , input.vehicle_journey_item_capacity
            ));

            MATHFP_TRY_LET(
                  CapacityAwareDemandSplitResult
                , capacity_split
                , iterate_capacity_aware_split(
                      choice_result
                    , input.input
                    , input.params
                    , input.demand_segment_time
                    , input.capacity_aware_assignment
                    , input.vehicle_journey_item_capacity.capacities
                )
            );
            MATHFP_TRY(validate_capacity_aware_split_step_output(
                  capacity_split
                , choice_result
                , input.input
                , input.capacity_aware_assignment
            ));
            MATHFP_TRY_LET(
                  CapacityAwareAssignmentDiagnostics
                , capacity_aware
                , make_capacity_aware_assignment_diagnostics(
                      true
                    , false
                    , capacity_split.diagnostics
                    , Dimless{ capacity_factor }
                    , input.capacity_aware_assignment.penalty_policy
                )
            );

            if (!capacity_split.diagnostics.converged) {
                log(
                      "capacity-aware split reached max_iterations before convergence; using last evaluated split"
                    , LogLevel::Warning
                );
            }

            split_result = std::move(capacity_split.split_result);
            return SplitStepResult{
                  .result         = std::move(split_result)
                , .capacity_aware = capacity_aware
            };
        } else {
            MATHFP_TRY(validate_split_step_input(
                  choice_result
                , input.input
                , input.params.split
                , input.demand_segment_time
            ));
            MATHFP_TRY_LET(
                  DemandSplitResult
                , ordinary_split
                , split_demand_over_connections(
                      choice_result
                    , input.input
                    , input.params
                    , input.demand_segment_time
                )
            );
            split_result = std::move(ordinary_split);
        }

        MATHFP_TRY(validate_split_step_output(
              split_result
            , choice_result
            , input.input
        ));
        return SplitStepResult{
              .result = std::move(split_result)
            , .capacity_aware =
                  make_capacity_aware_assignment_disabled_diagnostics(
                      input.capacity_aware_assignment.penalty_policy
                  )
        };
    }

    inline mathfp::Expected<DemandSplitResult> run_validated_single_split_step(
          const ConnectionChoiceResult&       choice_result
        , const AssignmentInput&              input
        , const VehicleJourneyItemLoadState&  fixed_load_state
    ) {
        if (capacity_aware_split_enabled(input)) {
            MATHFP_TRY(validate_capacity_aware_split_step_input(
                  choice_result
                , input.input
                , input.params
                , input.demand_segment_time
                , input.capacity_aware_assignment
                , input.vehicle_journey_item_capacity
            ));
            MATHFP_TRY_LET(
                  DemandSplitResult
                , capacity_split
                , split_demand_over_connections_capacity_aware(
                      choice_result
                    , input.input
                    , input.params
                    , input.demand_segment_time
                    , input.capacity_aware_assignment
                    , fixed_load_state
                    , input.vehicle_journey_item_capacity.capacities
                )
            );
            MATHFP_TRY(validate_split_step_output(
                  capacity_split
                , choice_result
                , input.input
            ));
            return capacity_split;
        }

        MATHFP_TRY(validate_split_step_input(
              choice_result
            , input.input
            , input.params.split
            , input.demand_segment_time
        ));
        MATHFP_TRY_LET(
              DemandSplitResult
            , ordinary_split
            , split_demand_over_connections(
                  choice_result
                , input.input
                , input.params
                , input.demand_segment_time
            )
        );
        MATHFP_TRY(validate_split_step_output(
              ordinary_split
            , choice_result
            , input.input
        ));
        return ordinary_split;
    }

    inline mathfp::Expected<AssignmentPipelineCalculatedResult>
    run_capacity_aware_assignment_iteration_layer(
          AssignmentInput      input
        , PreprocessedNetwork  network
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("assignment: capacity-aware fixed-point iteration");
        log(
            fmt::format(
                  "capacity-aware assignment input: max_iterations = {}  abs_tol = {}  rel_tol = {}  search_mode = {}  split_enabled = {}  penalty_policy = {}"
                , input.capacity_aware_assignment.iteration.max_iterations
                , input.capacity_aware_assignment.iteration.absolute_load_tolerance
                , input.capacity_aware_assignment.iteration.relative_load_tolerance
                , to_string(input.capacity_aware_assignment.search_mode)
                , capacity_aware_split_enabled(input) ? "true" : "false"
                , to_string(input.capacity_aware_assignment.penalty_policy)
            )
            , LogLevel::Info
        );

        VehicleJourneyItemLoadState current_state{};
        ConnectionSearchResult      last_search{};
        ConnectionChoiceResult      last_choice{};
        DemandSplitResult           last_split{};
        VehicleJourneyItemLoads     last_loads{};
        CapacityAwareSplitDiagnostics diagnostics{
              .enabled                 = true
            , .iterations              = 0
            , .converged               = false
            , .max_load_delta          = 0.0
            , .max_relative_load_delta = 0.0
        };

        for (std::int32_t iteration = 1;
             iteration <= input.capacity_aware_assignment.iteration.max_iterations;
             ++iteration) {
            /*
             * The load state is fixed for the whole search/choice/split
             * evaluation below. This is the correctness condition that keeps
             * branch-and-bound dominance and suffix lower bounds valid under
             * capacity-aware search: one iteration optimizes one immutable
             * generalized-cost function.
             */
            MATHFP_TRY_LET(
                  SearchStepResult
                , search_step
                , run_validated_search_step(
                      network
                    , input
                    , current_state
                    , SearchDiagnosticsContext{
                          .capacity_iteration = iteration
                      }
                  )
            );
            MATHFP_TRY_LET(
                  ConnectionChoiceResult
                , choice_result
                , run_validated_choice_step(
                      search_step.result
                    , input.params
                    , search_step.search_cost
                    , input.choice
                    , input.assignment_period
                    , ConnectionAdmissibilityConfig{
                          .deletion    = input.connection_deletion
                        , .demand_time = input.demand_segment_time
                      }
                )
            );
            MATHFP_TRY_LET(
                  DemandSplitResult
                , split_result
                , run_validated_single_split_step(
                      choice_result
                    , input
                    , current_state
                )
            );
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoads
                , split_loads
                , build_vehicle_journey_item_loads(split_result)
            );

            const auto alpha = 1.0 / static_cast<double>(iteration);
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoadState
                , next_state
                , msa_update_load_state(
                      current_state
                    , split_loads
                    , alpha
                )
            );

            const auto delta = load_state_delta(current_state, next_state);
            diagnostics.iterations              = iteration;
            diagnostics.max_load_delta          = delta.max_absolute;
            diagnostics.max_relative_load_delta = delta.max_relative;

            last_search = std::move(search_step.result);
            last_choice = std::move(choice_result);
            last_split  = std::move(split_result);
            last_loads  = std::move(split_loads);
            current_state = std::move(next_state);

            if (capacity_iteration_converged(
                  input.capacity_aware_assignment.iteration
                , delta
            )) {
                diagnostics.converged = true;
                break;
            }
        }

        MATHFP_TRY(validate_capacity_aware_split_step_output(
              CapacityAwareDemandSplitResult{
                    .split_result = last_split
                  , .split_loads  = last_loads
                  , .load_state   = current_state
                  , .diagnostics  = diagnostics
              }
            , last_choice
            , input.input
            , input.capacity_aware_assignment
        ));
        MATHFP_TRY_LET(
              CapacityAwareAssignmentDiagnostics
            , capacity_aware
            , make_capacity_aware_assignment_diagnostics(
                  true
                , true
                , diagnostics
                , capacity_aware_assignment_used_factor(input)
                , input.capacity_aware_assignment.penalty_policy
            )
        );

        if (!diagnostics.converged) {
            log(
                  "capacity-aware assignment reached max_iterations before convergence; using last evaluated search/choice/split"
                , LogLevel::Warning
            );
        }

        log(
            fmt::format(
                  "capacity-aware assignment result: iterations = {}  converged = {}  max_load_delta = {}  max_relative_load_delta = {}"
                , diagnostics.iterations
                , diagnostics.converged ? "true" : "false"
                , diagnostics.max_load_delta
                , diagnostics.max_relative_load_delta
            )
            , LogLevel::Info
        );
        both("assignment: capacity-aware fixed-point iteration done");

        return AssignmentPipelineCalculatedResult{
              .input                         = std::move(input.input)
            , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
            , .network                       = std::move(network)
            , .search                        = std::move(last_search)
            , .choice                        = std::move(last_choice)
            , .split                         = std::move(last_split)
            , .execution                     = input.execution
            , .skim_config                   = input.skim_matrix
            , .capacity_aware                = capacity_aware
        };
    }

    inline mathfp::Expected<AssignmentPipelineResult> run_timetable_assignment_pipeline_with_context(
        AssignmentInput input
    ) {
        timetable::infra::progress::both(
              "assignment pipeline started"
            , timetable::infra::LogLevel::Info
        );
        MATHFP_TRY(validate_assignment_execution_config(input.execution));

        if (!input.execution.calculate_assignment) {
            timetable::infra::progress::both(
                  "assignment pipeline disabled by basePara.calcAssign=false"
                , timetable::infra::LogLevel::Info
            );
            return AssignmentPipelineDisabledResult{
                  .input                         = std::move(input.input)
                , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
                , .execution                     = input.execution
                , .skim_config                   = input.skim_matrix
                , .capacity_aware                =
                      make_capacity_aware_assignment_disabled_diagnostics(
                          input.capacity_aware_assignment.penalty_policy
                      )
            };
        }

        MATHFP_TRY_LET(
              PreprocessedNetwork
            , network
            , run_validated_preprocessing_step(input)
        );

        if (input.search_execution.result_projection
            == SearchResultProjection::CompletionTargets) {
            MATHFP_TRY_LET(
                  SearchStepResult
                , search_step
                , run_validated_search_step(network, input, VehicleJourneyItemLoadState{})
            );
            if (!search_step.all_zone_result.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("all-zone search projection did not materialize all-zone result")
                );
            }
            return AssignmentPipelineAllZoneSearchResult{
                  .input                         = std::move(input.input)
                , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
                , .network                       = std::move(network)
                , .search                        = std::move(*search_step.all_zone_result)
                , .execution                     = input.execution
                , .skim_config                   = input.skim_matrix
                , .capacity_aware                =
                      make_capacity_aware_assignment_disabled_diagnostics(
                          input.capacity_aware_assignment.penalty_policy
                      )
            };
        }

        if (capacity_aware_search_enabled(input)) {
            MATHFP_TRY_LET(
                  AssignmentPipelineCalculatedResult
                , result
                , run_capacity_aware_assignment_iteration_layer(
                      std::move(input)
                    , std::move(network)
                )
            );
            return result;
        }

        MATHFP_TRY_LET(
              SearchStepResult
            , search_step
            , run_validated_search_step(network, input, VehicleJourneyItemLoadState{})
        );
        MATHFP_TRY_LET(
              ConnectionChoiceResult
            , choice_result
            , run_validated_choice_step(
                  search_step.result
                , input.params
                , search_step.search_cost
                , input.choice
                , input.assignment_period
                , ConnectionAdmissibilityConfig{
                      .deletion    = input.connection_deletion
                    , .demand_time = input.demand_segment_time
                  }
            )
        );
        MATHFP_TRY_LET(
              SplitStepResult
            , split_step
            , run_validated_split_step(
                  choice_result
                , input
            )
        );

        return AssignmentPipelineCalculatedResult{
              .input                         = std::move(input.input)
            , .vehicle_journey_item_capacity = std::move(input.vehicle_journey_item_capacity)
            , .network                       = std::move(network)
            , .search                        = std::move(search_step.result)
            , .choice                        = std::move(choice_result)
            , .split                         = std::move(split_step.result)
            , .execution                     = input.execution
            , .skim_config                   = input.skim_matrix
            , .capacity_aware                = split_step.capacity_aware
        };
    }

}  // namespace timetable::domain::assignment::detail
