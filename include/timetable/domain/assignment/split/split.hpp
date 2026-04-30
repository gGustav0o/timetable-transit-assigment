#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"

namespace timetable::domain::assignment {

    struct ConnectionDemandShare final {
        ZoneId               origin{};
        ZoneId               destination{};
        IntervalId           interval{};
        SearchConnection     connection;
        double               passengers{};
        double               probability{};
        double               independence{};
        double               split_impedance{};
    };

    struct DemandSplitResult final {
        std::vector<ConnectionDemandShare> shares{};
    };

    /**
     * @brief Split each demand entry over the chosen alternatives of its task.
     */
    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
          const ConnectionChoiceResult& choice_result
        , const InputModel&           input
        , const SearchParams&         params
        , const DemandSegmentTimeConfig& demand_segment_time
    );

}  // namespace timetable::domain::assignment
