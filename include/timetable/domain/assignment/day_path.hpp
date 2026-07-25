#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/day_path/alternative.hpp"
#include "timetable/domain/assignment/day_path/finalization.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&       retention
        , SearchConnection        connection
        , const SearchCostContext& search_cost
        , IntervalId              interval
    );

    [[nodiscard]] mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , SearchConnection              connection
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const DayPathRetentionConfig& config
    );

    [[nodiscard]] mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , DayPathSignature              signature
        , SearchConnection              connection
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const DayPathRetentionConfig& config
    );

    [[nodiscard]] mathfp::Expected<DayPathAlternative> make_day_path_alternative(
          SearchConnection        connection
        , const SearchCostContext& search_cost
        , IntervalId              interval
    );

    [[nodiscard]] mathfp::Expected<std::vector<SearchConnection>>
    retain_day_path_representative_connections(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    );

    [[nodiscard]] mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    );

    [[nodiscard]] mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const ChoiceTolerances&       tolerances
        , ChoiceRolloutStage            rollout_stage
    );

}  // namespace timetable::domain::assignment
