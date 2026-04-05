#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"

#include <fmt/format.h>

namespace timetable::domain::assignment {

    SearchPruningConfigSummary summarize(
        const SearchPruningConfig& config
    ) noexcept {
        return SearchPruningConfigSummary{
              .requested_state_space = config.model.requested_state_space
            , .rollout_stage         = config.runtime.rollout_stage
            , .current_state_space   =
                config.model.requested_state_space
                    == SearchPruningStateSpace::CurrentPhysicalAndOccurrence
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
            , static_cast<std::int64_t>(summary.requested_state_space)
            , static_cast<std::int64_t>(summary.rollout_stage)
            , summary.current_state_space ? "true" : "false"
        );
    }

    std::string format_search_pruning_execution_summary(
        const SearchPruningExecutionSummary& summary
    ) {
        return fmt::format(
              "search-pruning execution: state_space={} rollout_stage={} exact={} approximate={} approximate_policy={} exact_contract={}"
            , static_cast<std::int64_t>(summary.state_space)
            , static_cast<std::int64_t>(summary.rollout_stage)
            , summary.exact_enabled          ? "on"   : "off"
            , summary.approximate_enabled    ? "on"   : "off"
            , summary.has_approximate_policy ? "true" : "false"
            , static_cast<std::int64_t>(summary.exact_contract)
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
            , summary.inserted_labels
            , summary.skipped_insertions
        );
    }

}  // namespace timetable::domain::assignment
