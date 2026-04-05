#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"
#include <timetable/domain/segments.hpp>

namespace timetable::domain::assignment {

    struct DiscoveredConnection final {
        ZoneId                           origin{};
        ZoneId                           destination{};
        Time                             departure{};
        Time                             arrival{};
        Time                             journey_time{};
        Time                             transfer_time{};
        TransferCount                    transfers{};
        double                           fare{};
        double                           impedance{};
        std::vector<ConnectionSegmentId> segments{};
    };

    struct ConnectionSearchResult final {
        std::vector<DiscoveredConnection> connections{};
    };

    /**
     * @brief Enumerate feasible connections using timetable-based branch & bound.
     */
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , double                            fare_scale
        , const SearchParams&               params
        , const SearchPruningExecutionPlan* pruning_execution     = nullptr
        , const SearchTimeDomainExecution*  time_domain_execution = nullptr
    );

}  // namespace timetable::domain::assignment
