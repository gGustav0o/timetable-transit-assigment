#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/problem.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    struct SearchBatchKey final {
        ZoneId                        origin;
        std::optional<IntervalId>      interval{};
        std::vector<SearchTimeWindow> departure_windows{};
    };

    [[nodiscard]] bool operator<(
          const SearchBatchKey& lhs
        , const SearchBatchKey& rhs
    ) noexcept;

    /**
     * @brief Search execution batch.
     *
     * This is scheduling/projection infrastructure. The kernel input derived
     * from it is SearchProblem.
     */
    struct SearchBatch final {
        SearchBatchKey                         key{};
        std::reference_wrapper<const SearchTimeDomain> departure_domain;
        std::vector<SearchCompletionTarget>    completion_targets{};
        std::vector<SearchProjectionSlot>      projection_slots{};
    };

    struct SearchBatchExecutionDiagnostics final {
        std::size_t completion_target_count{};
        std::size_t projection_task_count{};
        std::size_t zero_completion_target_tree_count{};
        std::size_t zero_projection_task_tree_count{};
        std::size_t max_completion_targets_per_tree{};
        std::size_t max_projection_tasks_per_tree{};
    };

    [[nodiscard]] SearchBatchExecutionDiagnostics summarize_search_batches(
        std::span<const SearchBatch> batches
    ) noexcept;

    mathfp::Expected<std::vector<SearchBatch>> build_origin_period_search_batches(
          std::span<const SearchTask>    tasks
        , std::span<const SearchTreeJob> tree_jobs
        , SearchResultProjection         result_projection
    );

    [[nodiscard]] std::vector<SearchBatch> build_interval_local_search_batches(
        std::span<const SearchTask> tasks
    );

}  // namespace timetable::domain::assignment
