#pragma once

#include <cstddef>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/split/policies.hpp"
#include "timetable/domain/assignment/split/types.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    struct SplitImpedanceAlternativeView final {
        Time   departure_time{};
        Time   arrival_time{};
        double perceived_journey_time{};
        double fare{};
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_split_impedance_view(
          SplitImpedanceAlternativeView view
        , std::size_t                   index
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_split_impedance_policy(
        const SplitImpedancePolicy& policy
    );

    [[nodiscard]] Time split_reference_time(
          SplitImpedanceAlternativeView alternative
        , SplitReferenceTimeBasis       basis
    ) noexcept;

    [[nodiscard]] double split_early_deviation(
          Time                reference_time
        , const TimeInterval& interval
    ) noexcept;

    [[nodiscard]] double split_late_deviation(
          Time                reference_time
        , const TimeInterval& interval
    ) noexcept;

    [[nodiscard]] mathfp::Expected<double> split_temporal_utility(
          SplitImpedanceAlternativeView alternative
        , const TimeInterval&           interval
        , const SplitTemporalUtilityPolicy& policy
    );

    [[nodiscard]] mathfp::Expected<SplitRawImpedance> compute_split_impedance(
          SplitImpedanceAlternativeView alternative
        , const TimeInterval&           interval
        , const SplitImpedancePolicy&   policy
    );

    [[nodiscard]] mathfp::Expected<SplitTransformedImpedance> apply_impedance_transform(
          SplitRawImpedance original_impedance
        , const SplitImpedanceTransformPolicy& policy
    );

}  // namespace timetable::domain::assignment
