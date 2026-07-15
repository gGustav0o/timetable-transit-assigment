#include "timetable/domain/assignment/search/problem.hpp"

#include <cstdint>
#include <map>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_search_problem(
        const SearchProblem& problem
    ) {
        MATHFP_TRY(validate_search_time_domain(problem.departure_domain));

        if (problem.destinations.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search problem must contain at least one destination")
                    .ctx("origin", problem.origin.get())
            );
        }

        std::map<ZoneId, mathfp::Unit> seen;
        for (const auto destination : problem.destinations) {
            const auto [_, inserted] = seen.emplace(destination, mathfp::kUnit);
            if (!inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search problem contains duplicate destination")
                        .ctx("origin", problem.origin.get())
                        .ctx("destination", destination.get())
                );
            }
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<SearchProblem> make_search_problem(
          ZoneId           origin
        , SearchTimeDomain departure_domain
        , std::vector<ZoneId> destinations
    ) {
        SearchProblem problem{
              .origin           = origin
            , .departure_domain = std::move(departure_domain)
            , .destinations     = std::move(destinations)
        };
        MATHFP_TRY(validate_search_problem(problem));
        return problem;
    }

}  // namespace timetable::domain::assignment
