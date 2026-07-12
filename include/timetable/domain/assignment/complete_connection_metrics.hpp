#pragma once

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    //tex:
    // Complete connection metrics are the retained branch-and-bound comparison
    // coordinates for a finished connection:
    // $$(DEP(c),ARR(c),JT(c),NT(c),IMP(c)).$$
    // Fare is not duplicated here because it is already folded into $$IMP(c)$$
    // through the active SearchCostContext and remains recoverable from the
    // canonical SearchConnection metrics when needed for output/split.
    struct CompleteConnectionMetrics final {
        Time          departure{};
        Time          arrival{};
        Time          journey_time{};
        TransferCount transfers{ TransferCount{0} };
        double        impedance{};
    };

}  // namespace timetable::domain::assignment
