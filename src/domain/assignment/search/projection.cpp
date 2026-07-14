#include "timetable/domain/assignment/search/projection.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    SearchProjectionSlot make_demand_task_projection_slot(
          const SearchTask& task
        , std::size_t       result_index
    ) noexcept {
        return SearchProjectionSlot{
              .kind         = SearchProjectionSlotKind::DemandTask
            , .origin       = task.origin
            , .destination  = task.destination
            , .interval     = task.interval.id
            , .task_ref     = task.index
            , .task         = std::cref(task)
            , .result_index = result_index
        };
    }

    mathfp::Expected<std::vector<SearchProjectionSlot>> build_demand_projection_slots(
        std::span<const SearchTask> tasks
    ) {
        std::vector<SearchProjectionSlot> slots;
        slots.reserve(tasks.size());
        std::map<SearchTaskRef, mathfp::Unit> seen;
        for (std::size_t i = 0; i < tasks.size(); ++i) {
            const auto [_, inserted] = seen.emplace(tasks[i].index, mathfp::kUnit);
            if (!inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("duplicate search task ref while building demand projection slots")
                        .ctx("task", tasks[i].index.get())
                );
            }
            slots.push_back(make_demand_task_projection_slot(tasks[i], i));
        }
        return slots;
    }

    std::vector<SearchProjectionSlot> build_completion_target_projection_slots(
        const SearchTreeJob& job
    ) {
        std::vector<SearchProjectionSlot> slots;
        slots.reserve(job.completion_targets.size());
        for (const auto& target : job.completion_targets) {
            slots.push_back(
                SearchProjectionSlot{
                      .kind              = SearchProjectionSlotKind::CompletionTarget
                    , .origin            = job.origin
                    , .destination       = target.destination
                    , .interval          = std::nullopt
                    , .result_index      = std::nullopt
                    , .completion_target = target.index
                }
            );
        }
        return slots;
    }

    std::vector<SearchProjectionSlot> build_od_day_pair_projection_slots(
        const SearchTreeJob& job
    ) {
        std::vector<SearchProjectionSlot> slots;
        slots.reserve(job.completion_targets.size());
        for (const auto& target : job.completion_targets) {
            slots.push_back(
                SearchProjectionSlot{
                      .kind              = SearchProjectionSlotKind::OdDayPair
                    , .origin            = job.origin
                    , .destination       = target.destination
                    , .interval          = std::nullopt
                    , .result_index      = std::nullopt
                    , .completion_target = target.index
                }
            );
        }
        return slots;
    }

}  // namespace timetable::domain::assignment
