#pragma once

#include <mathfp/types/strong_type.hpp>

namespace timetable::domain::assignment {

    struct SplitLogWeightTag;
    using SplitLogWeight = mathfp::StrongType<double, SplitLogWeightTag>;

    struct SplitDemandMassTag;
    using SplitDemandMass = mathfp::StrongType<double, SplitDemandMassTag>;

    struct SplitProbabilityMassTag;
    using SplitProbabilityMass = mathfp::StrongType<double, SplitProbabilityMassTag>;

    struct SplitPassengerMassTag;
    using SplitPassengerMass = mathfp::StrongType<double, SplitPassengerMassTag>;

    struct SplitRawImpedanceTag;
    using SplitRawImpedance =
        mathfp::StrongType<double, SplitRawImpedanceTag>;

    struct SplitTransformedImpedanceTag;
    using SplitTransformedImpedance =
        mathfp::StrongType<double, SplitTransformedImpedanceTag>;

    struct SplitIndependenceWeightTag;
    using SplitIndependenceWeight =
        mathfp::StrongType<double, SplitIndependenceWeightTag>;

    struct SplitIndependenceInfluenceTag;
    using SplitIndependenceInfluence =
        mathfp::StrongType<double, SplitIndependenceInfluenceTag>;

}  // namespace timetable::domain::assignment
