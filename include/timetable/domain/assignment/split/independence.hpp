#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/split/types.hpp"
#include "timetable/domain/params/split.hpp"

namespace timetable::domain::assignment {

    struct SplitIndependenceAlternativeView final {
        double departure_time{};
        double arrival_time{};
        double perceived_journey_time{};
        double fare{};
    };

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_split_independence(
        SplitIndependenceWeight independence
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_split_independence_view(
          SplitIndependenceAlternativeView view
        , std::size_t                      index
    );

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_split_independence_config(
        const SplitIndependenceConfig& config
    );

    [[nodiscard]] mathfp::Expected<SplitIndependenceInfluence> split_connection_influence(
          const SplitIndependenceConfig&        config
        , SplitIndependenceAlternativeView      base
        , SplitIndependenceAlternativeView      other
    );

    [[nodiscard]] mathfp::Expected<SplitIndependenceWeight> compute_split_independence(
          const SplitIndependenceConfig&                  config
        , std::span<const SplitIndependenceAlternativeView> alternatives
        , std::size_t                                     index
    );

    [[nodiscard]] mathfp::Expected<std::vector<SplitIndependenceWeight>>
    compute_split_independences(
          const SplitIndependenceConfig&                  config
        , std::span<const SplitIndependenceAlternativeView> alternatives
    );

}  // namespace timetable::domain::assignment
