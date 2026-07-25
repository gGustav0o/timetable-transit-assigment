#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/split/policies.hpp"
#include "timetable/domain/assignment/split/types.hpp"

namespace timetable::domain::assignment {

    struct LoadAccumulationResult final {
        std::vector<double> segment_loads{};
        std::vector<double> stop_loads{};
        std::vector<double> trip_loads{};
        std::size_t         significant_loads{};
        double              total_load{};
    };

    [[nodiscard]] mathfp::Expected<LoadAccumulationResult> accumulate_loads(
          std::span<const SplitPassengerMass> passenger_passengers
        , std::span<const std::size_t>       trip_indices
        , std::span<const std::size_t>       stop_indices
        , std::span<const std::size_t>       segment_indices
        , const LoadAccumulationPolicy&       policy = {}
    );

}  // namespace timetable::domain::assignment
