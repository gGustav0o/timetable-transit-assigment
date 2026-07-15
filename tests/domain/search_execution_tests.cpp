#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/projection/contract.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"

namespace timetable::domain::assignment {
namespace {

    using test_support::completion_target;
    using test_support::domain;
    using test_support::task;

}  // namespace

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
              .index              = SearchTreeJobRef{ 0 }
            , .origin             = ZoneId{ 1 }
            , .departure_domain   = domain(0.0, 500.0)
            , .completion_targets = {
                  completion_target(0, 2),
                  completion_target(1, 3)
              }
            , .projection_tasks   = { SearchTaskRef{ 0 }, SearchTaskRef{ 1 } }
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

TEST(SearchProjectionContract, OdDayProductionRejectsStructuralDayLevelSupply) {
    std::vector<SearchTask> tasks{};
    std::vector<SearchTreeJob> jobs{
        SearchTreeJob{
              .index              = SearchTreeJobRef{ 0 }
            , .origin             = ZoneId{ 1 }
            , .departure_domain   = domain(0.0, 500.0)
            , .completion_targets = {
                  completion_target(0, 2)
              }
        }
    };

    const auto batches_result = build_origin_period_search_batches(
          std::span<const SearchTask>{ tasks.data(), tasks.size() }
        , std::span<const SearchTreeJob>{ jobs.data(), jobs.size() }
        , SearchResultProjection::OdDayPairs
    );
    ASSERT_TRUE(batches_result.has_value()) << batches_result.error().message();

    SearchPruningExecutionPlan pruning_execution{};
    pruning_execution.approximate_policy = ApproximatePruningPolicy{};

    const auto accepted = validate_od_day_production_batch_contract(
          batches_result->front()
        , SearchPartialRetentionScope::TreeGlobal
        , pruning_execution
        , false
    );
    EXPECT_TRUE(accepted.has_value()) << accepted.error().message();

    const auto rejected = validate_od_day_production_batch_contract(
          batches_result->front()
        , SearchPartialRetentionScope::TreeGlobal
        , pruning_execution
        , true
    );
    EXPECT_FALSE(rejected.has_value());
}

}  // namespace timetable::domain::assignment
