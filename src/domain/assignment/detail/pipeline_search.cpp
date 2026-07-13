#include "pipeline_steps.hpp"

#include <span>
#include <utility>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/complete_connection_diagnostics.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain_plan.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail {
    namespace {

        [[nodiscard]] SearchExecutionMode pipeline_search_execution_mode(
            const AssignmentInput& input
        ) noexcept {
            return input.search_execution.mode;
        }

        mathfp::Expected<std::optional<SearchTimeDomainExecution>>
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

        mathfp::Expected<PreprocessedNetwork> build_preprocessed_step(
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

    }  // namespace

    mathfp::Expected<PreprocessedNetwork> run_validated_preprocessing_step(
        AssignmentInput& input
    ) {
        MATHFP_TRY(validate_preprocessing_step_input(input));
        return build_preprocessed_step(input);
    }

    mathfp::Expected<PreparedSearchStep> prepare_validated_search_step(
          const PreprocessedNetwork& net
        , const AssignmentInput&     input
        , const VehicleJourneyItemLoadState& fixed_load_state
        , SearchDiagnosticsContext diagnostics
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
        search_diagnostics.declared_zone_count          = input.input.zones.size();
        search_diagnostics.validate_phase_invariants =
            input.search_execution.validate_phase_invariants;
        search_diagnostics.log_projection_details =
            input.search_execution.log_projection_details;

        return PreparedSearchStep{
              .tasks                 = std::move(search_tasks)
            , .time_domain_execution = std::move(search_time_domain_execution)
            , .pruning_execution     = std::move(search_pruning_execution)
            , .fare_scale            = fare_scale
            , .search_cost           = std::move(search_cost)
            , .diagnostics           = search_diagnostics
        };
    }

    SearchExecutionRequest make_search_execution_request(
          const AssignmentInput&       input
        , const PreparedSearchStep&    prepared
    ) noexcept {
        return SearchExecutionRequest{
              .config = input.search_execution
            , .time_domain_execution = prepared.time_domain_execution.has_value()
                ? &*prepared.time_domain_execution
                : nullptr
            , .declared_zones = std::span<const Zone>{
                  input.input.zones.data()
                , input.input.zones.size()
              }
        };
    }

    mathfp::Expected<SearchStepResult> run_validated_search_step(
          const PreprocessedNetwork& net
        , const AssignmentInput&     input
        , const VehicleJourneyItemLoadState& fixed_load_state
        , SearchDiagnosticsContext diagnostics
    ) {
        MATHFP_TRY_LET(
              PreparedSearchStep
            , prepared
            , prepare_validated_search_step(
                  net
                , input
                , fixed_load_state
                , diagnostics
            )
        );
        const auto execution_request = make_search_execution_request(input, prepared);

        if (input.search_execution.result_projection
            == SearchResultProjection::CompletionTargets) {
            MATHFP_TRY_LET(
                  AllZoneConnectionSearchResult
                , all_zone_search_result
                , search_all_zone_connections_branch_and_bound(
                      net
                    , prepared.tasks
                    , execution_request
                    , input.params
                    , prepared.search_cost
                    , input.choice
                    , input.assignment_period
                    , ConnectionAdmissibilityConfig{
                          .deletion    = input.connection_deletion
                        , .demand_time = input.demand_segment_time
                    }
                    , &prepared.pruning_execution
                    , input.complete_connection_dominance
                    , prepared.diagnostics
                )
            );
            return SearchStepResult{
                  .result          = {}
                , .all_zone_result = std::move(all_zone_search_result)
                , .fare_scale      = prepared.fare_scale
                , .search_cost     = std::move(prepared.search_cost)
            };
        }

        MATHFP_TRY_LET(
              ConnectionSearchResult
            , search_result
            , search_connections_branch_and_bound(
                  net
                , prepared.tasks
                , execution_request
                , input.params
                , prepared.search_cost
                , input.choice
                , input.assignment_period
                , ConnectionAdmissibilityConfig{
                      .deletion    = input.connection_deletion
                    , .demand_time = input.demand_segment_time
                }
                , &prepared.pruning_execution
                , input.complete_connection_dominance
                , prepared.diagnostics
            )
        );
        MATHFP_TRY(validate_search_step_output(
              search_result
            , net
            , prepared.fare_scale
            , input.params
            , input.assignment_period
            , ConnectionAdmissibilityConfig{
                  .deletion    = input.connection_deletion
                , .demand_time = input.demand_segment_time
              }
        ));
        return SearchStepResult{
              .result     = std::move(search_result)
            , .fare_scale = prepared.fare_scale
            , .search_cost = std::move(prepared.search_cost)
        };
    }


}  // namespace timetable::domain::assignment::detail
