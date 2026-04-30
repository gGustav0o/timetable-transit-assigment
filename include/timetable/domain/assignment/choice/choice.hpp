#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/search/search.hpp"

namespace timetable::domain::assignment {

    struct ChoiceTaskResult final {
        SearchTask                    task{};
        // Alternatives chosen for exactly this OD-interval task. Empty means
        // no feasible retained alternative was available for the task.
        std::vector<SearchConnection> connections{};
    };

    struct ConnectionChoiceResult final {
        // Unique flat projection used only for output indexing and validation.
        // Behavioral split must consume task_results, not this projection.
        std::vector<SearchConnection> connections{};
        std::vector<ChoiceTaskResult> task_results{};
    };

    /**
     * @brief Apply choice criteria to remove dominated/illogical connections.
     */
    mathfp::Expected<ConnectionChoiceResult> choose_connections(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , double                        fare_scale
        , const ChoiceConfig&           config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

}  // namespace timetable::domain::assignment
