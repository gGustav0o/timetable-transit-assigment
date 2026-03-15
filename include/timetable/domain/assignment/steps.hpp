#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/branch_state.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"

namespace timetable::domain::assignment {

    struct ConnectionSearchResult final {};

    struct ConnectionChoiceResult final {};

    struct DemandSplitResult final {};

    /**
     * @brief Enumerate feasible connections using timetable-based branch & bound.
     */
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const PreprocessedNetwork& network
        , double fare_scale
        , const SearchParams& params
    );

    /**
     * @brief Apply choice criteria to remove dominated/illogical connections.
     */
    mathfp::Expected<ConnectionChoiceResult> choose_connections(
        const ConnectionSearchResult& search_result
        , const SearchParams& params
    );

    /**
     * @brief Split OD demand over remaining connections.
     */
    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
        const ConnectionChoiceResult& choice_result
        , const InputModel& input
        , const SearchParams& params
    );

}  // namespace timetable::domain::assignment
