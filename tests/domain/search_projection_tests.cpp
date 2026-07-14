#include <vector>

#include <gtest/gtest.h>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/projection.hpp"

namespace timetable::domain::assignment {
namespace {

    using test_support::completion_target;
    using test_support::domain;
    using test_support::task;

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

TEST(SearchProjectionSlot, CompletionTargetSlotsCarryOnlyTargetIdentity) {
    const SearchTreeJob job{
          .index              = SearchTreeJobRef{ 0 }
        , .origin             = ZoneId{ 1 }
        , .departure_domain   = domain(0.0, 500.0)
        , .completion_targets = {
              completion_target(0, 2),
              completion_target(1, 3)
          }
        , .projection_tasks   = { SearchTaskRef{ 7 } }
    };

    const auto slots = build_completion_target_projection_slots(job);

    ASSERT_EQ(slots.size(), 2u);
    EXPECT_EQ(slots[0].kind, SearchProjectionSlotKind::CompletionTarget);
    EXPECT_EQ(slots[0].origin, ZoneId{ 1 });
    EXPECT_EQ(slots[0].destination, ZoneId{ 2 });
    EXPECT_FALSE(slots[0].task_ref.has_value());
    EXPECT_FALSE(slots[0].task.has_value());
    ASSERT_TRUE(slots[0].completion_target.has_value());
    EXPECT_EQ(*slots[0].completion_target, SearchCompletionTargetRef{ 0 });
}

TEST(SearchProjectionSlot, OdDayPairSlotsAreDayLevelPairProjections) {
    const SearchTreeJob job{
          .index              = SearchTreeJobRef{ 0 }
        , .origin             = ZoneId{ 1 }
        , .departure_domain   = domain(0.0, 500.0)
        , .completion_targets = {
              completion_target(0, 2)
          }
        , .projection_tasks   = { SearchTaskRef{ 7 } }
    };

    const auto slots = build_od_day_pair_projection_slots(job);

    ASSERT_EQ(slots.size(), 1u);
    EXPECT_EQ(slots[0].kind, SearchProjectionSlotKind::OdDayPair);
    EXPECT_EQ(slots[0].origin, ZoneId{ 1 });
    EXPECT_EQ(slots[0].destination, ZoneId{ 2 });
    EXPECT_FALSE(slots[0].interval.has_value());
    EXPECT_FALSE(slots[0].task.has_value());
}

}  // namespace timetable::domain::assignment
