#include <functional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/search/kernel/search_kernel.hpp"

namespace timetable::domain::assignment {
namespace {

    SearchTimeDomain domain(double begin, double end) {
        return SearchTimeDomain{
            .windows = {
                SearchTimeWindow{
                      .begin = Time{ begin }
                    , .end   = Time{ end }
                }
            }
        };
    }

    TimeInterval interval(std::int64_t id, double start, double end) {
        return TimeInterval{
              .id    = IntervalId{ id }
            , .start = Time{ start }
            , .end   = Time{ end }
        };
    }

    SearchTask task(
          std::int64_t index
        , std::int64_t origin
        , std::int64_t destination
        , std::int64_t interval_id
        , double       window_begin
        , double       window_end
    ) {
        return SearchTask{
              .index            = SearchTaskRef{ index }
            , .origin           = ZoneId{ origin }
            , .destination      = ZoneId{ destination }
            , .interval         = interval(interval_id, window_begin, window_end)
            , .departure_domain = domain(window_begin, window_end)
        };
    }

}  // namespace

TEST(SearchProjectionSlot, DemandTaskSlotCarriesOptionalTaskReference) {
    const auto demand_task = task(7, 1, 2, 3, 100.0, 200.0);

    const auto slot = make_demand_task_projection_slot(demand_task, 11u);

    EXPECT_EQ(slot.kind, SearchProjectionSlotKind::DemandTask);
    ASSERT_TRUE(slot.task.has_value());
    EXPECT_EQ(&slot.task->get(), &demand_task);
    ASSERT_TRUE(slot.task_ref.has_value());
    EXPECT_EQ(*slot.task_ref, demand_task.index);
    ASSERT_TRUE(slot.result_index.has_value());
    EXPECT_EQ(*slot.result_index, 11u);
}

TEST(SearchBatchBuilder, IntervalLocalBatchesKeepNonNullDepartureDomainReference) {
    std::vector<SearchTask> tasks{
        task(0, 1, 2, 10, 100.0, 200.0),
        task(1, 1, 3, 10, 100.0, 200.0),
        task(2, 1, 2, 20, 300.0, 400.0)
    };

    const auto batches = build_interval_local_search_batches(
        std::span<const SearchTask>{ tasks.data(), tasks.size() }
    );

    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(&batches[0].departure_domain.get(), &tasks[0].departure_domain);
    EXPECT_EQ(batches[0].projection_slots.size(), 2u);
    EXPECT_EQ(batches[0].completion_targets.size(), 2u);
    EXPECT_EQ(&batches[1].departure_domain.get(), &tasks[2].departure_domain);
    EXPECT_EQ(batches[1].projection_slots.size(), 1u);
    EXPECT_EQ(batches[1].completion_targets.size(), 1u);
}

TEST(SearchBatchBuilder, OriginPeriodBatchesUseTreeJobDomainAndProjectionSlots) {
    std::vector<SearchTask> tasks{
        task(0, 1, 2, 10, 100.0, 200.0),
        task(1, 1, 3, 20, 300.0, 400.0)
    };
    std::vector<SearchTreeJob> jobs{
        SearchTreeJob{
              .index = SearchTreeJobRef{ 0 }
            , .origin = ZoneId{ 1 }
            , .departure_domain = domain(0.0, 500.0)
            , .completion_targets = {
                SearchCompletionTarget{
                      .index = SearchCompletionTargetRef{ 0 }
                    , .destination = ZoneId{ 2 }
                },
                SearchCompletionTarget{
                      .index = SearchCompletionTargetRef{ 1 }
                    , .destination = ZoneId{ 3 }
                }
              }
            , .projection_tasks = { SearchTaskRef{ 0 }, SearchTaskRef{ 1 } }
        }
    };

    const auto batches_result = build_origin_period_search_batches(
          std::span<const SearchTask>{ tasks.data(), tasks.size() }
        , std::span<const SearchTreeJob>{ jobs.data(), jobs.size() }
        , SearchResultProjection::DemandTasks
    );

    ASSERT_TRUE(batches_result.has_value()) << batches_result.error().message();
    const auto& batches = *batches_result;
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(&batches[0].departure_domain.get(), &jobs[0].departure_domain);
    ASSERT_EQ(batches[0].projection_slots.size(), 2u);
    ASSERT_TRUE(batches[0].projection_slots[0].task.has_value());
    EXPECT_EQ(&batches[0].projection_slots[0].task->get(), &tasks[0]);
    ASSERT_TRUE(batches[0].projection_slots[1].task.has_value());
    EXPECT_EQ(&batches[0].projection_slots[1].task->get(), &tasks[1]);
}

}  // namespace timetable::domain::assignment
