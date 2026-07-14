#pragma once

#include <optional>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment.hpp"
#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/pipeline.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"
#include "timetable/domain/assignment/od_day_path_search.hpp"
#include "timetable/domain/assignment/search/branch_and_bound_search.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"
#include "timetable/domain/assignment/split.hpp"

namespace timetable::domain::assignment::detail {

    // Cross-module value contract for the internal pipeline implementation.
    // Keep local helpers in anonymous namespaces inside their owning .cpp.
    struct SearchStepResult final {
        ConnectionSearchResult                    result{};
        std::optional<AllZoneConnectionSearchResult> all_zone_result{};
        double                                    fare_scale{};
        SearchCostContext                         search_cost{};
    };

    struct PreparedSearchStep final {
        std::vector<SearchTask>                   tasks{};
        std::optional<SearchTimeDomainExecution>  time_domain_execution{};
        SearchPruningExecutionPlan                pruning_execution{};
        double                                    fare_scale{};
        SearchCostContext                         search_cost{};
        SearchDiagnosticsContext                  diagnostics{};
    };

    struct SplitStepResult final {
        DemandSplitResult                  result{};
        CapacityAwareAssignmentDiagnostics capacity_aware{};
    };

    [[nodiscard]] bool capacity_aware_search_enabled(
        const AssignmentInput& input
    ) noexcept;

    [[nodiscard]] bool capacity_aware_split_enabled(
        const AssignmentInput& input
    ) noexcept;

    [[nodiscard]] Dimless capacity_aware_assignment_used_factor(
        const AssignmentInput& input
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_required_od_day_search_execution(
        const SearchExecutionConfig& config
    );

    mathfp::Expected<mathfp::Unit> validate_assignment_search_execution_profile(
        const SearchExecutionConfig& config
    );

    mathfp::Expected<mathfp::Unit> validate_assignment_output_export_profile(
          const AssignmentExecutionConfig& execution
        , const SearchExecutionConfig&     search_execution
    );

    mathfp::Expected<mathfp::Unit> validate_od_day_origin_load_result(
        const OriginDayDemandLoadResult& result
    );

    mathfp::Expected<mathfp::Unit> validate_od_day_primary_load_contour(
          const DemandSplitResult&       split
        , const ElementarySegmentLoads&  elementary_segment_loads
    );

    void log_search_execution_summary(
        const SearchExecutionConfig& config
    );

    void log_assignment_output_export_profile(
        const AssignmentExecutionConfig& execution
    );

    mathfp::Expected<PreprocessedNetwork> run_validated_preprocessing_step(
        AssignmentInput& input
    );

    mathfp::Expected<PreparedSearchStep> prepare_validated_search_step(
          const PreprocessedNetwork& net
        , const AssignmentInput&     input
        , const VehicleJourneyItemLoadState& fixed_load_state
        , SearchDiagnosticsContext diagnostics
    );

    [[nodiscard]] SearchExecutionRequest make_search_execution_request(
          const AssignmentInput&       input
        , const PreparedSearchStep&    prepared
    ) noexcept;

    mathfp::Expected<SearchStepResult> run_validated_search_step(
          const PreprocessedNetwork& net
        , const AssignmentInput&     input
        , const VehicleJourneyItemLoadState& fixed_load_state
        , SearchDiagnosticsContext diagnostics
    );

    mathfp::Expected<ConnectionChoiceResult> run_validated_choice_step(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , const SearchCostContext&      search_cost
        , const ChoiceConfig&           config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    mathfp::Expected<SplitStepResult> run_validated_split_step(
          const ConnectionChoiceResult& choice_result
        , const AssignmentInput&        input
    );

    mathfp::Expected<DemandSplitResult> run_validated_single_split_step(
          const ConnectionChoiceResult&       choice_result
        , const AssignmentInput&              input
        , const VehicleJourneyItemLoadState&  fixed_load_state
    );

    mathfp::Expected<AssignmentPipelineOdDayCalculatedResult> run_od_day_assignment_layer(
          AssignmentInput      input
        , PreprocessedNetwork  network
    );

    mathfp::Expected<AssignmentPipelineCalculatedResult>
    run_capacity_aware_assignment_iteration_layer(
          AssignmentInput      input
        , PreprocessedNetwork  network
    );

}  // namespace timetable::domain::assignment::detail
