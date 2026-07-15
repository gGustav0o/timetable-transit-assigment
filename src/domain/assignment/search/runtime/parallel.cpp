#include "timetable/domain/assignment/search/runtime/parallel.hpp"

#include <algorithm>
#include <thread>

namespace timetable::domain::assignment::runtime {

    std::size_t search_batch_worker_count(
          std::size_t                  batch_count
        , const SearchExecutionConfig& config
    ) noexcept {
        if (batch_count == 0u || config.max_parallel_batches == 0u) {
            return 0u;
        }
        const auto hardware = std::max(
              1u
            , std::thread::hardware_concurrency()
        );
        auto worker_count = std::min(
              batch_count
            , std::min<std::size_t>(
                  static_cast<std::size_t>(hardware)
                , config.max_parallel_batches
              )
        );
        if (config.max_parallel_memory_mb.has_value()
            && config.estimated_memory_mb_per_parallel_batch > 0u) {
            const auto memory_limited_workers = std::max<std::size_t>(
                  1u
                , *config.max_parallel_memory_mb
                    / config.estimated_memory_mb_per_parallel_batch
            );
            worker_count = std::min(worker_count, memory_limited_workers);
        }
        return worker_count;
    }

    std::optional<std::size_t> claim_next_search_batch(
          SearchParallelBatchRuntime& runtime
        , std::size_t                 batch_count
    ) noexcept {
        const auto index = runtime.next_batch.fetch_add(
              1u
            , std::memory_order_relaxed
        );
        if (index >= batch_count) {
            return std::nullopt;
        }
        return index;
    }

    void mark_search_batch_completed(
        SearchParallelBatchRuntime& runtime
    ) noexcept {
        runtime.completed_batches.fetch_add(1u, std::memory_order_relaxed);
    }

    void mark_search_batch_cancelled(
        SearchParallelBatchRuntime& runtime
    ) noexcept {
        runtime.cancelled_batches.fetch_add(1u, std::memory_order_relaxed);
    }

    std::size_t completed_search_batches(
        const SearchParallelBatchRuntime& runtime
    ) noexcept {
        return runtime.completed_batches.load(std::memory_order_relaxed);
    }

    std::size_t cancelled_search_batches(
        const SearchParallelBatchRuntime& runtime
    ) noexcept {
        return runtime.cancelled_batches.load(std::memory_order_relaxed);
    }

}  // namespace timetable::domain::assignment::runtime
