#include "timetable/domain/assignment/search/runtime/batch_planning.hpp"

#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment::runtime::detail {

    mathfp::Expected<SearchBatchPlan> plan_search_batches(
          std::span<const SearchTask>   tasks
        , const SearchExecutionRequest& execution
        , SearchDiagnosticsContext      diagnostics
        , SearchBatchPlanningOptions    options
    ) {
        auto tree_jobs = std::vector<SearchTreeJob>{};
        auto batches = std::vector<SearchBatch>{};

        if (execution.config.mode == SearchExecutionMode::OriginPeriod) {
            if (!execution.time_domain_execution.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search requires SearchTimeDomainExecution")
                );
            }

            const auto& time_domain_execution =
                execution.time_domain_execution->get();
            MATHFP_TRY_LET(
                  std::vector<SearchTreeJob>
                , planned_tree_jobs
                , execution.config.origin_scope == SearchOriginScope::DeclaredZones
                    ? build_declared_origin_period_search_tree_jobs(
                          execution.declared_zones
                        , tasks
                        , time_domain_execution
                        , execution.config.destination_scope
                      )
                    : build_origin_period_search_tree_jobs(
                          tasks
                        , time_domain_execution
                        , execution.config.destination_scope
                        , execution.declared_zones
                      )
            );
            tree_jobs = std::move(planned_tree_jobs);
            MATHFP_TRY_LET(
                  std::vector<SearchBatch>
                , planned_batches
                , build_origin_period_search_batches(
                      tasks
                    , tree_jobs
                    , execution.config.result_projection
                )
            );
            batches = std::move(planned_batches);
        } else {
            batches = build_interval_local_search_batches(tasks);
        }

        MATHFP_TRY(validate_search_batch_projection_contract(
              batches
            , execution.config.mode
            , execution.config.result_projection
            , tasks
        ));

        auto plan = SearchBatchPlan{
              .tree_jobs = std::move(tree_jobs)
            , .batches = std::move(batches)
            , .batch_diagnostics = {}
            , .expected_tree_count = expected_search_tree_count(
                  execution.config.origin_scope
                , diagnostics.declared_zone_count
                , tasks
              )
            , .time_domain_summary = std::nullopt
        };
        plan.batch_diagnostics = summarize_search_batches(plan.batches);

        if (options.summarize_time_domains) {
            MATHFP_TRY_LET(
                  SearchTimeDomainSummary
                , summary
                , summarize_batch_search_domains(plan.batches)
            );
            plan.time_domain_summary = std::move(summary);
        }

        return plan;
    }

}  // namespace timetable::domain::assignment::runtime::detail
