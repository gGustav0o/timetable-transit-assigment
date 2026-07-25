#pragma once

#include <cstdint>
#include <limits>

#include "timetable/domain/params/split.hpp"

namespace timetable::domain::assignment {

    struct IndependencePolicy {
        SplitIndependenceConfig config;
    };

    enum class SplitReferenceTimeBasis : std::uint8_t {
          Departure
        , Arrival
    };

    struct SplitTemporalUtilityPolicy {
        SplitReferenceTimeBasis basis{ SplitReferenceTimeBasis::Departure };
        TemporalUtilityWeights  weights{};
    };

    struct SplitImpedancePolicy {
        Dimless                    q_time{};
        Dimless                    q_departure{};
        Dimless                    q_fare{};
        SplitTemporalUtilityPolicy temporal_utility{};
    };

    struct SplitImpedanceTransformPolicy {
        SplitImpedanceTransformConfig config;
    };

    struct ChoiceWeightPolicy {
        SplitChoiceModelConfig model_config;
    };

    struct ProbabilityPolicy {
        double numerical_suppression_threshold =
            std::numeric_limits<double>::epsilon();
        double relative_tolerance_coefficient =
            std::numeric_limits<double>::epsilon();
    };

    struct DemandProjectionPolicy {
        double significant_probability_threshold = 1e-10;
        double passenger_count_tolerance = 1e-6;
    };

    struct LoadAccumulationPolicy {
        double load_accumulation_threshold = 1e-8;
    };

}  // namespace timetable::domain::assignment
