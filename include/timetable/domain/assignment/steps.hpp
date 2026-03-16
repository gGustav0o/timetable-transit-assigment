#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/branch_state.hpp"
#include "timetable/domain/assignment/preprocessed_network.hpp"

namespace timetable::domain::assignment {

    struct DiscoveredConnection final {
        ZoneId origin{};
        ZoneId destination{};
        Time departure{};
        Time arrival{};
        Time journey_time{};
        Time transfer_time{};
        TransferCount transfers{};
        double fare{};
        double impedance{};
        std::vector<ConnectionSegmentId> segments{};
    };

    struct ConnectionSearchResult final {
        std::vector<DiscoveredConnection> connections{};
    };

    struct ConnectionChoiceResult final {
        std::vector<DiscoveredConnection> connections{};
    };

    struct ConnectionDemandShare final {
        ZoneId origin{};
        ZoneId destination{};
        IntervalId interval{};
        DiscoveredConnection connection{};
        double passengers{};
        double probability{};
        double independence{};
        double split_impedance{};
    };

    struct DemandSplitResult final {
        std::vector<ConnectionDemandShare> shares{};
    };

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
