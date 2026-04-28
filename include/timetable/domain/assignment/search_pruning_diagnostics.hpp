#pragma once

#include <cstdint>
#include <string>

#include "timetable/domain/assignment/search_pruning_config.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"

namespace timetable::domain::assignment {

    struct SearchPruningConfigSummary final {
        SearchPruningStateSpace  requested_state_space{
            SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
        };
        SearchPruningRolloutStage rollout_stage{
            SearchPruningRolloutStage::ExactAndApproximateCurrentState
        };
        bool current_state_space{};
    };

    struct SearchPruningExecutionSummary final {
        SearchPruningStateSpace  state_space{
            SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
        };
        SearchPruningRolloutStage rollout_stage{
            SearchPruningRolloutStage::ExactAndApproximateCurrentState
        };
        bool exact_enabled{};
        bool approximate_enabled{};
        bool has_approximate_policy{};
        ExactDominanceContract exact_contract{
            ExactDominanceContract::ExtensionSafeCurrentState
        };
    };

    struct SearchPruningRuntimeStats final {
        std::size_t evaluated_candidates{};
        std::size_t accepted_candidates{};
        std::size_t rejected_exact{};
        std::size_t rejected_approximate{};
        std::size_t inserted_metrics{};
        std::size_t skipped_insertions{};
    };

    [[nodiscard]] SearchPruningConfigSummary summarize(
        const SearchPruningConfig& config
    ) noexcept;

    [[nodiscard]] SearchPruningExecutionSummary summarize(
        const SearchPruningExecutionPlan& plan
    ) noexcept;

    [[nodiscard]] SearchPruningRuntimeStats summarize(
        const SearchPruningRuntimeStats& stats
    ) noexcept;

    [[nodiscard]] std::string format_search_pruning_config_summary(
        const SearchPruningConfigSummary& summary
    );

    [[nodiscard]] std::string format_search_pruning_execution_summary(
        const SearchPruningExecutionSummary& summary
    );

    [[nodiscard]] std::string format_search_pruning_runtime_stats(
        const SearchPruningRuntimeStats& summary
    );

}  // namespace timetable::domain::assignment
