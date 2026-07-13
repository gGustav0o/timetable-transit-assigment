#pragma once

#include "timetable/domain/params/preprocess.hpp"
#include "timetable/domain/params/search_impedance.hpp"
#include "timetable/domain/params/split.hpp"
#include "timetable/domain/params/tolerances.hpp"
#include "timetable/domain/params/transfer_limits.hpp"

namespace timetable::domain {

    /**
     * @brief Full parameter bundle for timetable-based assignment.
     */
    struct SearchParams final {
        PreprocessParams preprocess{};
        SearchImpedance  impedance{};
        TransferLimits   transfers{};
        SearchTolerances search_tolerances{};
        ChoiceTolerances choice_tolerances{};
        SplitParams      split{};
    };

}  // namespace timetable::domain
