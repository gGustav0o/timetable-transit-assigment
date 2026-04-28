#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"

#include <fmt/format.h>

#include "detail/diagnostic_format.hpp"

namespace timetable::domain::assignment {

    SearchPruningConfigSummary summarize(
        const SearchPruningConfig& config
    ) noexcept {
        return SearchPruningConfigSummary{
              .requested_state_space = config.model.requested_state_space
            , .rollout_stage         = config.runtime.rollout_stage
            , .current_state_space   =
                config.model.requested_state_space
                    == SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
        };
    }

    SearchPruningExecutionSummary summarize(
        const SearchPruningExecutionPlan& plan
    ) noexcept {
        return SearchPruningExecutionSummary{
              .state_space            = plan.state_space
            , .rollout_stage          = plan.rollout_stage
            , .exact_enabled          = plan.exact_enabled
            , .approximate_enabled    = plan.approximate_enabled
            , .has_approximate_policy = plan.approximate_policy.has_value()
            , .exact_contract         = plan.exact_policy.contract
        };
    }

    SearchPruningRuntimeStats summarize(
        const SearchPruningRuntimeStats& stats
    ) noexcept {
        return stats;
    }

    std::string format_search_pruning_config_summary(
        const SearchPruningConfigSummary& summary
    ) {
        return fmt::format(
              "search-pruning config: requested_state_space={} rollout_stage={} current_state_space={}"
            , to_string(summary.requested_state_space)
            , to_string(summary.rollout_stage)
            , detail::diagnostic::bool_text(summary.current_state_space)
        );
    }

    std::string format_search_pruning_execution_summary(
        const SearchPruningExecutionSummary& summary
    ) {
        return fmt::format(
              "search-pruning execution: state_space={} rollout_stage={} exact={} approximate={} approximate_policy={} exact_contract={}"
            , to_string(summary.state_space)
            , to_string(summary.rollout_stage)
            , detail::diagnostic::enabled_text(summary.exact_enabled)
            , detail::diagnostic::enabled_text(summary.approximate_enabled)
            , detail::diagnostic::bool_text(summary.has_approximate_policy)
            , to_string(summary.exact_contract)
        );
    }

    std::string format_search_pruning_runtime_stats(
        const SearchPruningRuntimeStats& summary
    ) {
        return fmt::format(
              "search-pruning runtime: evaluated={} accepted={} rejected(exact/approx)={}/{} inserted={} skipped_insertions={}"
            , summary.evaluated_candidates
            , summary.accepted_candidates
            , summary.rejected_exact
            , summary.rejected_approximate
            , summary.inserted_metrics
            , summary.skipped_insertions
        );
    }

}  // namespace timetable::domain::assignment
