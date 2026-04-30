#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/choice.hpp"
#include "timetable/domain/assignment/skim_config.hpp"
#include "timetable/domain/assignment/split.hpp"
#include "timetable/domain/model.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief One scalar observation for skim aggregation.
     *
     * value is a metric value of one connection alternative; weight is either
     * passenger volume or an equal alternative weight, depending on
     * SkimMatrixConfig::volume_weighted.
     */
    struct SkimWeightedValue final {
        double value{};
        double weight{};
    };

    /**
     * @brief Aggregated skim row for one OD pair and one demand interval.
     *
     * The row is a projection over the chosen connections and their split
     * shares. Time fields use the same time unit as the timetable input.
     * transfers is scalar because a mean skim can be fractional.
     */
    struct AssignmentSkimEntry final {
        ZoneId     origin{};
        ZoneId     destination{};
        IntervalId interval{};

        double      demand_passengers{};
        double      assigned_passengers{};
        std::size_t connection_count{};
        std::size_t included_connection_count{};

        Time   journey_time{};
        Time   in_vehicle_time{};
        Time   access_time{};
        Time   egress_time{};
        Time   walk_time{};
        Time   wait_time{};
        Time   transfer_wait_time{};
        Time   transfer_walk_time{};
        double transfers{};
        double fare{};
        double split_impedance{};
    };

    /**
     * @brief Domain-level skim matrix result.
     *
     * entries are ordered by the input demand-entry order. Duplicate demand
     * keys are rejected by the builder instead of being silently merged.
     */
    struct AssignmentSkimMatrix final {
        std::vector<AssignmentSkimEntry> entries{};
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_assignment_skim_matrix(
        const AssignmentSkimMatrix& skim_matrix
    );

    [[nodiscard]] mathfp::Expected<double> aggregate_skim_values(
          std::span<const SkimWeightedValue> values
        , const SkimMatrixConfig&            config
    );

    /**
     * @brief Convert lowImpConnShare to a retained alternative count.
     *
     * The value is interpreted as a fraction of the number of available
     * alternatives after sorting by increasing split impedance. For a positive
     * share and a non-empty support at least one alternative is retained.
     */
    [[nodiscard]] mathfp::Expected<std::size_t> low_impedance_connection_count(
          std::size_t connection_count
        , double      low_impedance_connection_share
    );

    [[nodiscard]] mathfp::Expected<AssignmentSkimMatrix> build_assignment_skim_matrix(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const DemandSplitResult&      split_result
        , const SkimMatrixConfig&       config
    );

}  // namespace timetable::domain::assignment
