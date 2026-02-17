#pragma once

#include <mathfp/types/units.hpp>

namespace timetable::domain {

    using Time    = mathfp::units::Quantity<double, mathfp::units::Time>;
    using Length  = mathfp::units::Quantity<double, mathfp::units::Length>;
    using Dimless = mathfp::units::Quantity<double, mathfp::units::Dimless>;
    using Speed   = mathfp::units::Quantity<double, mathfp::units::Dim<1, 0, -1, 0, 0, 0, 0>>;

}  // namespace timetable::domain

