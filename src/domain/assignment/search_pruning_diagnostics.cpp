#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"

#include <fmt/format.h>

#include "detail/diagnostic_format.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool use_last_stop_for_equivalent_connections(
            const EquivalentConnectionDominanceConfig& config
        ) noexcept {
            return config.stop_reference
                == EquivalentConnectionStopReference::LastTimedStopOccurrence;
        }

    }  // namespace

    SearchPruningConfigSummary summarize(
        const SearchPruningConfig& config
    ) noexcept {
        const auto retention_requested =
               config.runtime.rollout_stage != SearchPruningRolloutStage::Disabled
            && config.model.equivalent_connection_dominance.allow_dominance_for_equivalent_connections;

        return SearchPruningConfigSummary{
              .requested_state_space = config.model.requested_state_space
            , .rollout_stage         = config.runtime.rollout_stage
            , .equivalent_connection_dominance =
                config.model.equivalent_connection_dominance
            , .current_state_space   =
                config.model.requested_state_space
                    == SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
            , .retention_requested   = retention_requested
        };
    }

    SearchPruningExecutionSummary summarize(
        const SearchPruningExecutionPlan& plan
    ) noexcept {
        const auto retention_suppressed_by_equivalent_dominance =
               plan.rollout_stage != SearchPruningRolloutStage::Disabled
            && !plan.equivalent_connection_dominance.allow_dominance_for_equivalent_connections
            && !plan.exact_enabled
            && !plan.approximate_enabled
            && !plan.approximate_policy.has_value();

        return SearchPruningExecutionSummary{
              .state_space            = plan.state_space
            , .rollout_stage          = plan.rollout_stage
            , .equivalent_connection_dominance =
                plan.equivalent_connection_dominance
            , .exact_enabled          = plan.exact_enabled
            , .approximate_enabled    = plan.approximate_enabled
            , .has_approximate_policy = plan.approximate_policy.has_value()
            , .retention_suppressed_by_equivalent_dominance =
                retention_suppressed_by_equivalent_dominance
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
              "search-pruning config: requested_state_space={} rollout_stage={} equivalent_dominance={} equivalent_stop_reference={} use_last_stop_for_equivalent_connections={} current_state_space={} retention_requested={}"
            , to_string(summary.requested_state_space)
            , to_string(summary.rollout_stage)
            , detail::diagnostic::enabled_text(
                  summary.equivalent_connection_dominance.allow_dominance_for_equivalent_connections
            )
            , to_string(summary.equivalent_connection_dominance.stop_reference)
            , detail::diagnostic::bool_text(
                  use_last_stop_for_equivalent_connections(
                      summary.equivalent_connection_dominance
                  )
              )
            , detail::diagnostic::bool_text(summary.current_state_space)
            , detail::diagnostic::bool_text(summary.retention_requested)
        );
    }

    std::string format_search_pruning_execution_summary(
        const SearchPruningExecutionSummary& summary
    ) {
        return fmt::format(
              "search-pruning execution: state_space={} rollout_stage={} equivalent_dominance={} equivalent_stop_reference={} use_last_stop_for_equivalent_connections={} exact={} approximate={} approximate_policy={} retention_suppressed_by_equivalent_dominance={} exact_contract={}"
            , to_string(summary.state_space)
            , to_string(summary.rollout_stage)
            , detail::diagnostic::enabled_text(
                  summary.equivalent_connection_dominance.allow_dominance_for_equivalent_connections
              )
            , to_string(summary.equivalent_connection_dominance.stop_reference)
            , detail::diagnostic::bool_text(
                  use_last_stop_for_equivalent_connections(
                      summary.equivalent_connection_dominance
                  )
              )
            , detail::diagnostic::enabled_text(summary.exact_enabled)
            , detail::diagnostic::enabled_text(summary.approximate_enabled)
            , detail::diagnostic::bool_text(summary.has_approximate_policy)
            , detail::diagnostic::bool_text(
                  summary.retention_suppressed_by_equivalent_dominance
              )
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
