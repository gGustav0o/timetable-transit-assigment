#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain_plan.hpp"
#include "timetable/domain/assignment/split.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail {

    struct AssignmentPipelineResult final {
        InputModel             input{};
        PreprocessedNetwork    network{};
        ConnectionSearchResult search{};
        ConnectionChoiceResult choice{};
        DemandSplitResult      split{};
    };

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

    inline mathfp::Expected<ConnectionSearchResult> run_validated_search_step(
          const PreprocessedNetwork& net
        , const AssignmentInput&     input
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        const auto& params = input.params;
        MATHFP_TRY(validate_preprocessing_step_output(net, params));
        const auto fare_scale = compute_fare_scale(
              net.connection_segments
            , params.impedance.fare_normalization
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
                  input.search_pruning.model.requested_state_space
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
        MATHFP_TRY_LET(
              std::optional<SearchTimeDomainExecution>
            , search_time_domain_execution
            , prepare_search_time_domain_execution(
                  input.input
                , input.search_time_domain.model.padding_policy
                , params.split
                , input.search_time_domain.runtime.architecture
                , input.search_time_domain.runtime.rollout_stage
                , input.search_time_domain.model  .requested_mode
            )
        );
        MATHFP_TRY_LET(
              ConnectionSearchResult
            , search_result
            , search_connections_branch_and_bound(
                  net
                , fare_scale
                , params
                , &search_pruning_execution
                , search_time_domain_execution ? &*search_time_domain_execution : nullptr
            )
        );
        MATHFP_TRY(validate_search_step_output(
              search_result
            , net
            , fare_scale
            , params
        ));
        return mathfp::Expected<ConnectionSearchResult>(std::move(search_result));
    }

    inline mathfp::Expected<ConnectionChoiceResult> run_validated_choice_step(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , const ChoiceConfig&           config
    ) {
        MATHFP_TRY_LET(
              ConnectionChoiceResult
            , choice_result
            , choose_connections(search_result, params, config)
        );
        MATHFP_TRY(validate_choice_step_output(choice_result, search_result));
        return mathfp::Expected<ConnectionChoiceResult>(std::move(choice_result));
    }

    inline mathfp::Expected<DemandSplitResult> run_validated_split_step(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
    ) {
        MATHFP_TRY(validate_split_step_input(choice_result, input));
        MATHFP_TRY_LET(
              DemandSplitResult
            , split_result
            , split_demand_over_connections(choice_result, input, params)
        );
        MATHFP_TRY(validate_split_step_output(
              split_result
            , choice_result
            , input
        ));
        return mathfp::Expected<DemandSplitResult>(std::move(split_result));
    }

    inline mathfp::Expected<AssignmentPipelineResult> run_timetable_assignment_pipeline_with_context(
        AssignmentInput input
    ) {
        timetable::infra::progress::both(
              "assignment pipeline started"
            , timetable::infra::LogLevel::Info
        );

        MATHFP_TRY_LET(
              PreprocessedNetwork
            , network
            , run_validated_preprocessing_step(input)
        );
        MATHFP_TRY_LET(
              ConnectionSearchResult
            , search_result
            , run_validated_search_step(network, input)
        );
        MATHFP_TRY_LET(
              ConnectionChoiceResult
            , choice_result
            , run_validated_choice_step(search_result, input.params, input.choice)
        );
        MATHFP_TRY_LET(
              DemandSplitResult
            , split_result
            , run_validated_split_step(choice_result, input.input, input.params)
        );

        return AssignmentPipelineResult{
              .input   = std::move(input.input)
            , .network = std::move(network)
            , .search  = std::move(search_result)
            , .choice  = std::move(choice_result)
            , .split   = std::move(split_result)
        };
    }

}  // namespace timetable::domain::assignment::detail
