#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
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

    [[nodiscard]] mathfp::Expected<SearchPruningMetrics> make_partial_pruning_metrics(
          const SearchPartialMetrics& metrics
        , const SearchCostContext&    search_cost
    );

    [[nodiscard]] mathfp::Expected<SearchPruningMetrics> make_partial_pruning_metrics(
          const SearchBranch&      branch
        , const SearchCostContext& search_cost
    );

    [[nodiscard]] mathfp::Expected<SearchPruningMetrics> make_day_path_pruning_metrics(
          const SearchPartialMetrics& metrics
        , const SearchCostContext&    search_cost
    );

    [[nodiscard]] mathfp::Expected<SearchPruningMetrics> make_day_path_pruning_metrics(
          const SearchBranch&      branch
        , const SearchCostContext& search_cost
    );

}  // namespace timetable::domain::assignment
