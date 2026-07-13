#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/params/search_params.hpp"

namespace timetable::domain {

    inline mathfp::Expected<SearchParams> make_search_params(
          PreprocessParams preprocess
        , SearchImpedance  impedance
        , TransferLimits   transfers
        , SearchTolerances search_tolerances
        , ChoiceTolerances choice_tolerances
        , SplitParams      split
    ) {
        return SearchParams{
              .preprocess        = std::move(preprocess)
            , .impedance         = std::move(impedance)
            , .transfers         = std::move(transfers)
            , .search_tolerances = std::move(search_tolerances)
            , .choice_tolerances = std::move(choice_tolerances)
            , .split             = std::move(split)
        };
    }

}  // namespace timetable::domain
