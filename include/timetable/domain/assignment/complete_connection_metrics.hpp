#pragma once

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    struct CompleteConnectionMetrics final {
        Time          departure{};
        Time          arrival{};
        Time          journey_time{};
        TransferCount transfers{};
        double        impedance{};
    };

}  // namespace timetable::domain::assignment
