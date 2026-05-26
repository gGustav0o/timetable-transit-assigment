#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
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

    struct OdDayChoicePairResult final {
        ZoneId                        origin{};
        ZoneId                        destination{};
        std::vector<DayPathAlternative> alternatives{};
        std::vector<SearchConnection> connections{};
    };

    struct OriginDayChoiceResult final {
        ZoneId                             origin{};
        std::vector<OdDayChoicePairResult> pair_results{};
    };

    struct OdDayConnectionChoiceResult final {
        // Unique flat projection of chosen path representatives.
        std::vector<SearchConnection>      connections{};
        std::vector<OriginDayChoiceResult> origin_results{};
    };

    /**
     * @brief Apply choice criteria to remove dominated/illogical connections.
     */
    mathfp::Expected<ConnectionChoiceResult> choose_connections(
          const ConnectionSearchResult& search_result
        , const SearchParams&           params
        , const SearchCostContext&      search_cost
        , const ChoiceConfig&           config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    );

    /**
     * @brief Apply day-level choice pruning to OD-day alternatives.
     *
     * Demand-interval admissibility is deliberately not applied here. It belongs
     * to the split/load layer, where the same OD-day alternative set is evaluated
     * against each demand interval of the OD pair.
     */
    mathfp::Expected<OdDayConnectionChoiceResult> choose_od_day_connections(
          const OdDayConnectionSearchResult& search_result
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                config
    );

    mathfp::Expected<OriginDayChoiceResult> choose_origin_day_connections(
          const OriginDaySearchResult& search_result
        , const SearchParams&          params
        , const SearchCostContext&     search_cost
        , const ChoiceConfig&          config
    );

}  // namespace timetable::domain::assignment
