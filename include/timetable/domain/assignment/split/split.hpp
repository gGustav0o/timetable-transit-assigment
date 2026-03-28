#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"

namespace timetable::domain::assignment {

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
     * @brief Split OD demand over remaining connections.
     */
    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
        const ConnectionChoiceResult& choice_result
        , const InputModel& input
        , const SearchParams& params
    );

}  // namespace timetable::domain::assignment
