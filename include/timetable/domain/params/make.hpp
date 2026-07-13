#pragma once

#include "timetable/domain/params/make/preprocess.hpp"
#include "timetable/domain/params/make/search_impedance.hpp"
#include "timetable/domain/params/make/search_params.hpp"
#include "timetable/domain/params/make/split.hpp"
#include "timetable/domain/params/make/tolerances.hpp"
#include "timetable/domain/params/make/transfer_limits.hpp"

namespace timetable::domain {

    /*
     * Canonical parameter-constructor umbrella.
     *
     * Domain parameter structs are inert mathematical values. All construction
     * paths from external configuration, defaults and tests should enter the
     * domain through these functions so invariants stay in one place.
     */

}  // namespace timetable::domain
