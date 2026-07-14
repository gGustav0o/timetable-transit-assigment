#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/kernel_result.hpp"
#include "timetable/domain/assignment/search/policy.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/problem.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Run the timetable branch-and-bound kernel for one mathematical
     * search problem.
     */
    mathfp::Expected<SearchKernelResult> run_branch_and_bound(
          const PreprocessedNetwork& network
        , const SearchProblem&       problem
        , const SearchPolicy&        policy
    );

}  // namespace timetable::domain::assignment
