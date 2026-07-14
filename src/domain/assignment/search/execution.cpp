#include "timetable/domain/assignment/search/execution.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {

    bool operator<(
          const SearchBatchKey& lhs
        , const SearchBatchKey& rhs
    ) noexcept {
        if (lhs.origin != rhs.origin) {
            return lhs.origin < rhs.origin;
        }
        if (lhs.interval.has_value() != rhs.interval.has_value()) {
            return !lhs.interval.has_value() && rhs.interval.has_value();
        }
        if (lhs.interval.has_value() && *lhs.interval != *rhs.interval) {
            return *lhs.interval < *rhs.interval;
        }
        const auto common_size = std::min(
              lhs.departure_windows.size()
            , rhs.departure_windows.size()
        );
        for (std::size_t i = 0; i < common_size; ++i) {
            if (lhs.departure_windows[i].begin.value() != rhs.departure_windows[i].begin.value()) {
                return lhs.departure_windows[i].begin.value() < rhs.departure_windows[i].begin.value();
            }
            if (lhs.departure_windows[i].end.value() != rhs.departure_windows[i].end.value()) {
                return lhs.departure_windows[i].end.value() < rhs.departure_windows[i].end.value();
            }
        }
        return lhs.departure_windows.size() < rhs.departure_windows.size();
    }

    SearchBatchExecutionDiagnostics summarize_search_batches(
        std::span<const SearchBatch> batches
    ) noexcept {
        SearchBatchExecutionDiagnostics summary{};
        for (const auto& batch : batches) {
            summary.completion_target_count += batch.completion_targets.size();
            summary.projection_task_count   += batch.projection_slots.size();
            if (batch.completion_targets.empty()) {
                ++summary.zero_completion_target_tree_count;
            }
            if (batch.projection_slots.empty()) {
                ++summary.zero_projection_task_tree_count;
            }
            summary.max_completion_targets_per_tree = std::max(
                  summary.max_completion_targets_per_tree
                , batch.completion_targets.size()
            );
            summary.max_projection_tasks_per_tree = std::max(
                  summary.max_projection_tasks_per_tree
                , batch.projection_slots.size()
            );
        }
        return summary;
    }

    mathfp::Expected<std::vector<SearchBatch>> build_origin_period_search_batches(
          std::span<const SearchTask>    tasks
        , std::span<const SearchTreeJob> tree_jobs
        , SearchResultProjection         result_projection
    ) {
        std::map<SearchTaskRef, SearchProjectionSlot> slot_by_task;
        if (result_projection == SearchResultProjection::DemandTasks) {
            MATHFP_TRY_LET(
                  std::vector<SearchProjectionSlot>
                , demand_slots
                , build_demand_projection_slots(tasks)
            );
            for (const auto& slot : demand_slots) {
                slot_by_task.emplace(*slot.task_ref, slot);
            }
        }

        std::vector<SearchBatch> batches;
        batches.reserve(tree_jobs.size());
        for (const auto& job : tree_jobs) {
            SearchBatch batch{
                  .key = SearchBatchKey{
                        .origin = job.origin
                      , .interval = std::nullopt
                      , .departure_windows = job.departure_domain.windows
                  }
                , .departure_domain = std::cref(job.departure_domain)
                , .completion_targets = {}
                , .projection_slots = {}
            };
            batch.completion_targets = job.completion_targets;
            switch (result_projection) {
                case SearchResultProjection::DemandTasks:
                    batch.projection_slots.reserve(job.projection_tasks.size());
                    for (const auto task_ref : job.projection_tasks) {
                        const auto it = slot_by_task.find(task_ref);
                        if (it == slot_by_task.end()) {
                            return mathfp::unexpected(
                                mathfp::internal_error("origin-period search tree job references unknown search task")
                                    .ctx("job", job.index.get())
                                    .ctx("origin", job.origin.get())
                                    .ctx("task", task_ref.get())
                            );
                        }
                        batch.projection_slots.push_back(it->second);
                    }
                    break;

                case SearchResultProjection::OdDayPairs:
                    batch.projection_slots = build_od_day_pair_projection_slots(job);
                    break;

                case SearchResultProjection::CompletionTargets:
                    batch.projection_slots =
                        build_completion_target_projection_slots(job);
                    break;
                }
            batches.push_back(std::move(batch));
        }

        return batches;
    }

    std::vector<SearchBatch> build_interval_local_search_batches(
        std::span<const SearchTask> tasks
    ) {
        std::vector<SearchBatch> batches;
        std::map<SearchBatchKey, std::size_t> index_by_key;

        for (std::size_t i = 0; i < tasks.size(); ++i) {
            const auto& task = tasks[i];
            SearchBatchKey key{
                  .origin            = task.origin
                , .interval          = task.interval.id
                , .departure_windows = task.departure_domain.windows
            };

            const auto [it, inserted] = index_by_key.emplace(key, batches.size());
            if (inserted) {
                batches.push_back(
                    SearchBatch{
                          .key              = std::move(key)
                        , .departure_domain = std::cref(task.departure_domain)
                        , .completion_targets = {
                              SearchCompletionTarget{
                                    .index = SearchCompletionTargetRef{ 0 }
                                  , .destination = task.destination
                              }
                          }
                        , .projection_slots = {}
                    }
                );
            } else {
                auto& targets = batches[it->second].completion_targets;
                const auto exists = std::any_of(
                      targets.begin()
                    , targets.end()
                    , [&](const SearchCompletionTarget& target) {
                        return target.destination == task.destination;
                    }
                );
                if (!exists) {
                    targets.push_back(
                        SearchCompletionTarget{
                              .index = SearchCompletionTargetRef{
                                  static_cast<std::int64_t>(targets.size())
                              }
                            , .destination = task.destination
                        }
                    );
                }
            }

            batches[it->second].projection_slots.push_back(
                make_demand_task_projection_slot(task, i)
            );
        }

        return batches;
    }

}  // namespace timetable::domain::assignment
