#pragma once

#include <functional>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/kernel_result.hpp"
#include "timetable/domain/assignment/search/policy.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/problem.hpp"

namespace timetable::domain::assignment {

    struct SearchKernelInput final {
        std::reference_wrapper<const PreprocessedNetwork> network;
        SearchProblem                                     problem;
        SearchPolicy                                      policy;
    };

    mathfp::Expected<mathfp::Unit> validate_search_kernel_input(
        const SearchKernelInput& input
    );

    mathfp::Expected<SearchKernelInput> make_search_kernel_input(
          const PreprocessedNetwork& network
        , SearchProblem              problem
        , SearchPolicy               policy
    );

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
