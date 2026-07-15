#pragma once

#include <span>
#include <utility>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Pure branch-and-bound problem.
     *
     * This is the mathematical search input: one origin, one first-boarding
     * time domain and destination nodes. It deliberately contains no demand
     * interval, choice configuration, projection slot or output concern.
     */
    struct SearchProblem final {
        ZoneId              origin;
        SearchTimeDomain    departure_domain{};
        std::vector<ZoneId> destinations{};
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_problem(
        const SearchProblem& problem
    );

    [[nodiscard]] mathfp::Expected<SearchProblem> make_search_problem(
          ZoneId           origin
        , SearchTimeDomain departure_domain
        , std::vector<ZoneId> destinations
    );

    struct SearchTreeJobRefTag {};

    using SearchTreeJobRef = DomainId<SearchTreeJobRefTag>;

    struct SearchCompletionTargetRefTag {};

    using SearchCompletionTargetRef = DomainId<SearchCompletionTargetRefTag>;

    struct SearchCompletionTarget final {
        SearchCompletionTargetRef index;
        ZoneId                    destination;
    };

    /**
     * @brief Origin-keyed execution job.
     *
     * The first three fields define the kernel problem. projection_tasks are
     * execution/projection metadata and must not leak into SearchProblem.
     */
    struct SearchTreeJob final {
        SearchTreeJobRef                    index;
        ZoneId                              origin;
        SearchTimeDomain                    departure_domain{};
        std::vector<SearchCompletionTarget> completion_targets{};
        std::vector<SearchTaskRef>          projection_tasks{};
    };

    [[nodiscard]] inline SearchProblem search_problem_of(
        const SearchTreeJob& job
    ) {
        std::vector<ZoneId> destinations;
        destinations.reserve(job.completion_targets.size());
        for (const auto& target : job.completion_targets) {
            destinations.push_back(target.destination);
        }
        return SearchProblem{
              .origin           = job.origin
            , .departure_domain = job.departure_domain
            , .destinations     = std::move(destinations)
        };
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask> tasks
        , const SearchTimeDomain&     period_domain
    );

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
        , SearchDestinationScope            destination_scope
        , std::span<const Zone>             declared_zones
    );

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
    );

    mathfp::Expected<std::vector<SearchTreeJob>> build_declared_origin_period_search_tree_jobs(
          std::span<const Zone>             declared_zones
        , std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
        , SearchDestinationScope            destination_scope
    );

}  // namespace timetable::domain::assignment
