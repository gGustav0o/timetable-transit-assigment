#include "timetable/domain/assignment/search/kernel.hpp"

#include <cstdint>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_search_kernel_input(
        const SearchKernelInput& input
    ) {
        const auto& network = input.network.get();
        if (network.route_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search kernel input requires route segments")
            );
        }
        if (network.connection_segments.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search kernel input requires connection segments")
            );
        }
        MATHFP_TRY(validate_search_problem(input.problem));
        MATHFP_TRY(validate_search_policy(input.policy));
        return mathfp::kUnit;
    }

    mathfp::Expected<SearchKernelInput> make_search_kernel_input(
          const PreprocessedNetwork& network
        , SearchProblem              problem
        , SearchPolicy               policy
    ) {
        SearchKernelInput input{
              .network = std::cref(network)
            , .problem = std::move(problem)
            , .policy  = std::move(policy)
        };
        MATHFP_TRY(validate_search_kernel_input(input));
        return input;
    }

}  // namespace timetable::domain::assignment
