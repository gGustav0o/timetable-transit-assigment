#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/search_pruning_config.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/assignment/search_time_domain_builder.hpp"
#include "timetable/domain/assignment/split/split.hpp"

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_input(
        const AssignmentInput& input
    );

    mathfp::Expected<mathfp::Unit> validate_preprocessing_step_output(
          const PreprocessedNetwork& network
        , const SearchParams&      params
    );

    mathfp::Expected<mathfp::Unit> validate_search_step_output(
          const ConnectionSearchResult& result
        , const PreprocessedNetwork&  network
        , double                      fare_scale
        , const SearchParams&         params
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<mathfp::Unit> validate_choice_step_output(
          const ConnectionChoiceResult&   choice_result
        , const ConnectionSearchResult& search_result
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<mathfp::Unit> validate_split_step_input(
          const ConnectionChoiceResult& choice_result
        , const InputModel&           input
        , const SplitParams&          params
        , const DemandSegmentTimeConfig& demand_segment_time
    );

    mathfp::Expected<mathfp::Unit> validate_split_step_output(
          const DemandSplitResult&        split_result
        , const ConnectionChoiceResult& choice_result
        , const InputModel&             input
    );

    /**
     * @brief Validate the domain-level pruning request.
     *
     * Contract:
     * - the requested pruning state-space must be supported by the current
     *   domain model;
     * - the rollout stage must not contradict the currently supported
     *   state-space semantics.
     */
    mathfp::Expected<mathfp::Unit> validate_search_pruning_config(
        const SearchPruningConfig& config
    );

    /**
     * @brief Validate the executable pruning plan derived from config + tolerances.
     *
     * Contract:
     * - exact/approximate enablement must agree with the rollout stage;
     * - approximate pruning may only be enabled when an approximate policy is
     *   materialized;
     * - the exact contract must agree with the current state-space.
     */
    mathfp::Expected<mathfp::Unit> validate_search_pruning_execution_plan(
          const SearchPruningExecutionPlan& plan
        , const SearchTolerances&           tolerances
    );

    /**
     * @brief Validate inputs used to materialize OD-interval SearchTask objects.
     *
     * Contract:
     * - the assignment period must be finite and non-negative;
     * - intervals used by positive-demand entries must exist and satisfy start < end;
     * - the assignment-period expansion of every active interval must produce
     *   a finite valid departure-time domain.
     *
     * This is intentionally independent of SearchTimePaddingPolicy. Search tasks
     * are bounded by the assignment period, while SearchTimePaddingPolicy belongs
     * to search-time-domain planning/optimization.
     */
    mathfp::Expected<mathfp::Unit> validate_search_task_builder_input(
          const InputModel&              input
        , const AssignmentPeriodConfig&  assignment_period
    );

    /**
     * @brief Validate the mathematical inputs used to derive demand-induced search-time domains.
     *
     * Contract:
     * - intervals used by positive-demand entries must exist and satisfy start < end;
     * - the selected padding policy must be structurally valid;
     * - SplitTemporalUtility padding requires finite resolvability from the split
     *   departure-time term.
     */
    mathfp::Expected<mathfp::Unit> validate_search_time_domain_builder_input(
          const InputModel&               input
        , SearchWindowMode                mode
        , const SearchTimePaddingPolicy&  padding_policy
        , const SplitParams&              split
    );

    /**
     * @brief Validate the builder output against its requested mode and active-demand support.
     *
     * Contract:
     * - the catalog must satisfy its structural invariants;
     * - every active positive-demand slice must be covered by a non-null domain
     *   in the requested mode;
     * - inactive demand slices need not induce any stored domain.
     */
    mathfp::Expected<mathfp::Unit> validate_search_time_domain_builder_output(
          const SearchTimeDomainCatalog& catalog
        , const InputModel&              input
        , SearchWindowMode               expected_mode
    );

}  // namespace timetable::domain::assignment
