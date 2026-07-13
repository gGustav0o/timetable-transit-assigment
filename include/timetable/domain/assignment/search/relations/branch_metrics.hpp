#pragma once

#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/impedance.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] Time partial_journey_time(
        const SearchPartialMetrics& metrics
    ) noexcept;

    [[nodiscard]] Time partial_walk_time(
        const SearchPartialMetrics& metrics
    ) noexcept;

    [[nodiscard]] ConnectionImpedanceComponents partial_impedance_components(
        const SearchPartialMetrics& metrics
    ) noexcept;

}  // namespace timetable::domain::assignment
