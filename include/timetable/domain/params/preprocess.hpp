#pragma once

#include <cstdint>
#include <optional>

#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

    enum class WalkCostKind : std::uint8_t {
          Time
        , Length
        , Weighted
    };

    struct WalkCostWeights final {
        Dimless w_time{};
        Dimless w_length{};
    };

    enum class TimeAggregationKind : std::uint8_t {
          Mean
        , Median
        , Minimum
    };

    struct PreprocessParams final {
        WalkCostKind         walk_cost_kind            { WalkCostKind::Time };
        WalkCostWeights      walk_cost                 {};
        std::optional<Speed> line_speed                {};
        bool                 strict_trips              { true };
        bool                 allow_overnight           { false };
        bool                 overnight_add_24h         { true };
        bool                 strict_stop_times         { true };
        TimeAggregationKind  time_aggregation          { TimeAggregationKind::Mean };
        bool                 deduplicate_walk_segments { true };
        bool                 stable_ordering           { true };
    };

}  // namespace timetable::domain
