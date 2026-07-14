#include <gtest/gtest.h>

#include "search_test_support.hpp"

#include "timetable/domain/assignment/search/problem.hpp"

namespace timetable::domain::assignment {
namespace {

    using test_support::completion_target;
    using test_support::domain;

}  // namespace

TEST(SearchProblem, TreeJobProjectionKeepsOnlyKernelInputs) {
    const SearchTreeJob job{
          .index              = SearchTreeJobRef{ 9 }
        , .origin             = ZoneId{ 1 }
        , .departure_domain   = domain(0.0, 500.0)
        , .completion_targets = {
              completion_target(0, 2),
              completion_target(1, 3)
          }
        , .projection_tasks   = { SearchTaskRef{ 10 }, SearchTaskRef{ 11 } }
    };

    const auto problem = search_problem_of(job);

    EXPECT_EQ(problem.origin, job.origin);
    EXPECT_EQ(problem.departure_domain.windows.size(), 1u);
    ASSERT_EQ(problem.destinations.size(), 2u);
    EXPECT_EQ(problem.destinations[0], ZoneId{ 2 });
    EXPECT_EQ(problem.destinations[1], ZoneId{ 3 });
}

}  // namespace timetable::domain::assignment
