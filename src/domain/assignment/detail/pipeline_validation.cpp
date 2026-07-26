#include "pipeline_steps.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include <fmt/format.h>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::detail {

    mathfp::Expected<mathfp::Unit> validate_required_od_day_search_execution(
        const SearchExecutionConfig& config
    ) {
        if (!is_required_od_day_assignment_profile(config)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("required assignment must use OD-day search execution contract")
                    .ctx("formulation", std::string(to_string(config.formulation)))
                    .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                    .ctx("mode", std::string(to_string(config.mode)))
                    .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                    .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                    .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                    .ctx("result_projection", std::string(to_string(config.result_projection)))
                    .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                    .ctx("max_parallel_batches", static_cast<std::int64_t>(config.max_parallel_batches))
                    .ctx(
                          "max_parallel_memory_mb"
                        , config.max_parallel_memory_mb.has_value()
                            ? static_cast<std::int64_t>(*config.max_parallel_memory_mb)
                            : std::int64_t{ -1 }
                      )
                    .ctx(
                          "estimated_memory_mb_per_parallel_batch"
                        , static_cast<std::int64_t>(
                              config.estimated_memory_mb_per_parallel_batch
                          )
                      )
                    .ctx(
                          "default_max_parallel_batches"
                        , static_cast<std::int64_t>(
                              SearchExecutionConfig::kDefaultMaxParallelBatches
                          )
                      )
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_assignment_search_execution_profile(
        const SearchExecutionConfig& config
    ) {
        switch (config.formulation) {
            case AssignmentCalculationFormulation::OdDayAssignment:
                return validate_required_od_day_search_execution(config);

            case AssignmentCalculationFormulation::TimedConnectionDiagnostics:
                if (is_timed_connection_diagnostics_profile(config)) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("timed connection contour is available only as a diagnostic demand-task profile")
                        .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                        .ctx("mode", std::string(to_string(config.mode)))
                        .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                        .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                        .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                        .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                );

            case AssignmentCalculationFormulation::AllZoneSearch:
                if (is_all_zone_search_diagnostics_profile(config)) {
                    return mathfp::kUnit;
                }
                return mathfp::unexpected(
                    mathfp::invalid_arg("all-zone search is available only as a diagnostic completion-target profile")
                        .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                        .ctx("mode", std::string(to_string(config.mode)))
                        .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                        .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                        .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                        .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                );

            case AssignmentCalculationFormulation::DemandTaskAssignment:
                return mathfp::unexpected(
                    mathfp::invalid_arg("fallback demand-task timed assignment is disabled; use od_day_assignment for production or timed_connection_diagnostics for search-only diagnostics")
                        .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                        .ctx("mode", std::string(to_string(config.mode)))
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                        .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                );
        }

        return mathfp::unexpected(
            mathfp::invalid_arg("unsupported assignment calculation formulation")
                .ctx("formulation", static_cast<std::int64_t>(config.formulation))
        );
    }

    mathfp::Expected<mathfp::Unit> validate_assignment_output_export_profile(
          const AssignmentExecutionConfig& execution
        , const SearchExecutionConfig&     search_execution
    ) {
        if (execution.output_export_profile
                == AssignmentOutputExportProfile::DiagnosticFullPath
            && !search_execution.diagnostic_mode) {
            return mathfp::unexpected(
                mathfp::invalid_arg("full path-level output is diagnostic-only")
                    .ctx(
                          "output_export_profile"
                        , std::string(to_string(execution.output_export_profile))
                      )
                    .ctx(
                          "search_formulation"
                        , std::string(to_string(search_execution.formulation))
                      )
                    .ctx(
                          "diagnostic_mode"
                        , search_execution.diagnostic_mode ? "true" : "false"
                      )
            );
        }
        return mathfp::kUnit;
    }

    namespace {

        mathfp::Expected<mathfp::Unit> validate_od_day_split_sources(
            const DemandSplitResult& split
        ) {
            for (std::size_t i = 0; i < split.shares.size(); ++i) {
                if (split.shares[i].source != DemandShareAlternativeSource::DayPath) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day assignment split contains non-day-path share")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin", split.shares[i].origin.get())
                            .ctx("destination", split.shares[i].destination.get())
                    );
                }
                if (split.shares[i].day_path.origin != split.shares[i].origin
                    || split.shares[i].day_path.destination != split.shares[i].destination) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share day-path identity disagrees with OD demand key")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin", split.shares[i].origin.get())
                            .ctx("destination", split.shares[i].destination.get())
                            .ctx("path_origin", split.shares[i].day_path.origin.get())
                            .ctx("path_destination", split.shares[i].day_path.destination.get())
                    );
                }
                if (!split.shares[i].day_path_support.has_value()
                    || split.shares[i].day_path_support->signature != split.shares[i].day_path) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share compact support disagrees with day-path identity")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin", split.shares[i].origin.get())
                            .ctx("destination", split.shares[i].destination.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_od_day_origin_load_result(
        const OriginDayDemandLoadResult& result
    ) {
        MATHFP_TRY(validate_od_day_split_sources(result.split_result));
        MATHFP_TRY(validate_elementary_segment_load_projection(
              result.split_result
            , result.elementary_segment_loads
        ));
        if (result.split_result.shares.empty()
            && !result.elementary_segment_loads.items.empty()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day elementary loads cannot exist without day-path split shares")
                    .ctx(
                          "elementary_loads"
                        , static_cast<std::int64_t>(
                              result.elementary_segment_loads.items.size()
                          )
                      )
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_od_day_primary_load_contour(
          const DemandSplitResult&       split
        , const ElementarySegmentLoads&  elementary_segment_loads
    ) {
        MATHFP_TRY(validate_od_day_split_sources(split));
        MATHFP_TRY(validate_elementary_segment_load_projection(
              split
            , elementary_segment_loads
        ));
        return mathfp::kUnit;
    }

    void log_search_execution_summary(
        const SearchExecutionConfig& config
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        log(
            fmt::format(
                  "assignment search execution: formulation={} diagnostic={} mode={} origin_scope={} time_domain_source={} destination_scope={} result_projection={} partial_retention_scope={} max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={} validate_phase_invariants={} log_projection_details={}"
                , to_string(config.formulation)
                , config.diagnostic_mode ? "true" : "false"
                , to_string(config.mode)
                , to_string(config.origin_scope)
                , to_string(config.time_domain_source)
                , to_string(config.destination_scope)
                , to_string(config.result_projection)
                , to_string(config.partial_retention_scope)
                , config.max_parallel_batches
                , config.max_parallel_memory_mb.has_value()
                    ? std::to_string(*config.max_parallel_memory_mb)
                    : std::string("unbounded")
                , config.estimated_memory_mb_per_parallel_batch
                , config.validate_phase_invariants ? "true" : "false"
                , config.log_projection_details ? "true" : "false"
            )
            , LogLevel::Info
        );
        if (config.diagnostic_mode) {
            log(
                  "assignment search execution is diagnostic; it is not the required OD-day assignment mode"
                , LogLevel::Warning
            );
        }
        if (config.formulation == AssignmentCalculationFormulation::TimedConnectionDiagnostics) {
            log(
                  "timed connection contour is preserved only for diagnostics; production loading must use OD-day DayPath alternatives"
                , LogLevel::Warning
            );
        }
        if (config.formulation == AssignmentCalculationFormulation::OdDayAssignment) {
            log(
                fmt::format(
                      "production OD-day profile: day_path_search=true load_source=day_path primary_load=elementary_segment_loads workers={}/{} memory_cap_mb={} worker_memory_mb={} label_representatives=unbounded"
                    , config.max_parallel_batches
                    , SearchExecutionConfig::kDefaultMaxParallelBatches
                    , config.max_parallel_memory_mb.has_value()
                        ? std::to_string(*config.max_parallel_memory_mb)
                        : std::string("unbounded")
                    , config.estimated_memory_mb_per_parallel_batch
                )
                , LogLevel::Info
            );
        }
    }

    void log_assignment_output_export_profile(
        const AssignmentExecutionConfig& execution
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        log(
            fmt::format(
                  "assignment output export profile: profile={} production_files=summary,metadata,od_summary,share_summary,loads,stop_loads,elementary_segment_loads,skim_matrix,vehicle_journey_item_loads full_path_dump={}"
                , to_string(execution.output_export_profile)
                , execution.output_export_profile
                    == AssignmentOutputExportProfile::DiagnosticFullPath
                    ? "enabled_diagnostic"
                    : "disabled_by_default"
            )
            , LogLevel::Info
        );
    }


}  // namespace timetable::domain::assignment::detail
