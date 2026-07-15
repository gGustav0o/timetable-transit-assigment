#pragma once

#include <atomic>
#include <cstddef>
#include <future>
#include <optional>
#include <utility>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/runtime/cancellation.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"

namespace timetable::domain::assignment::runtime {

    struct SearchParallelBatchRuntime final {
        std::atomic<std::size_t> next_batch{ 0u };
        std::atomic<std::size_t> completed_batches{ 0u };
        std::atomic<std::size_t> cancelled_batches{ 0u };
        SearchCancellationToken  cancellation{};
    };

    enum class SearchParallelBatchStepStatus {
          Completed
        , Cancelled
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

    template <typename Step>
    [[nodiscard]] mathfp::Expected<mathfp::Unit> run_search_parallel_batches(
          SearchParallelBatchRuntime& runtime
        , std::size_t                 worker_count
        , std::size_t                 batch_count
        , Step&&                      step
    ) {
        std::vector<std::future<mathfp::Expected<mathfp::Unit>>> workers;
        workers.reserve(worker_count);

        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.push_back(
                std::async(
                      std::launch::async
                    , [&runtime, batch_count, worker, &step]()
                          -> mathfp::Expected<mathfp::Unit> {
                          for (;;) {
                              if (search_cancelled(&runtime.cancellation)) {
                                  return mathfp::kUnit;
                              }
                              const auto claimed_batch = claim_next_search_batch(
                                    runtime
                                  , batch_count
                              );
                              if (!claimed_batch.has_value()) {
                                  return mathfp::kUnit;
                              }

                              auto step_result = step(worker, *claimed_batch);
                              if (!step_result) {
                                  request_search_cancellation(
                                      &runtime.cancellation
                                  );
                                  return mathfp::unexpected(
                                      std::move(step_result.error())
                                  );
                              }
                              if (*step_result
                                  == SearchParallelBatchStepStatus::Cancelled) {
                                  mark_search_batch_cancelled(runtime);
                                  return mathfp::kUnit;
                              }
                              mark_search_batch_completed(runtime);
                          }
                      }
                )
            );
        }

        mathfp::Expected<mathfp::Unit> first_worker_error = mathfp::kUnit;
        for (auto& worker : workers) {
            auto worker_result = worker.get();
            if (!worker_result && first_worker_error) {
                request_search_cancellation(&runtime.cancellation);
                first_worker_error = mathfp::unexpected(
                    std::move(worker_result.error())
                );
            }
        }
        return first_worker_error;
    }

}  // namespace timetable::domain::assignment::runtime
