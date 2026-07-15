#pragma once

#include <atomic>
#include <cstddef>
#include <optional>

#include "timetable/domain/assignment/search/runtime/cancellation.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"

namespace timetable::domain::assignment::runtime {

    struct SearchParallelBatchRuntime final {
        std::atomic<std::size_t> next_batch{ 0u };
        std::atomic<std::size_t> completed_batches{ 0u };
        std::atomic<std::size_t> cancelled_batches{ 0u };
        SearchCancellationToken  cancellation{};
    };

    [[nodiscard]] std::size_t search_batch_worker_count(
          std::size_t                  batch_count
        , const SearchExecutionConfig& config
    ) noexcept;

    [[nodiscard]] std::optional<std::size_t> claim_next_search_batch(
          SearchParallelBatchRuntime& runtime
        , std::size_t                 batch_count
    ) noexcept;

    void mark_search_batch_completed(
        SearchParallelBatchRuntime& runtime
    ) noexcept;

    void mark_search_batch_cancelled(
        SearchParallelBatchRuntime& runtime
    ) noexcept;

    [[nodiscard]] std::size_t completed_search_batches(
        const SearchParallelBatchRuntime& runtime
    ) noexcept;

    [[nodiscard]] std::size_t cancelled_search_batches(
        const SearchParallelBatchRuntime& runtime
    ) noexcept;

}  // namespace timetable::domain::assignment::runtime
