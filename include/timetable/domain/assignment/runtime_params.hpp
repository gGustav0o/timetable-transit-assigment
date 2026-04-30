#pragma once

#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/skim_config.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain {

    /**
     * @brief Runtime parameter bundle parsed from params.txt.
     *
     * SearchParams remains the mathematical model used by preprocessing,
     * search, choice and split. SkimMatrixConfig is kept separate because it
     * configures an analytical result projection.
     * AssignmentPeriodConfig is likewise separate because it defines the
     * admissible assignment-time support around demand intervals.
     * Connection deletion and demand-segment time configs are kept outside
     * SearchParams because they decide admissibility of alternatives for
     * choice/split rather than search impedance or split weights.
     */
    struct AssignmentRuntimeParams final {
        SearchParams                         search{};
        assignment::SkimMatrixConfig         skim_matrix{};
        assignment::AssignmentPeriodConfig   assignment_period{};
        assignment::ConnectionDeletionConfig connection_deletion{};
        assignment::DemandSegmentTimeConfig  demand_segment_time{};
    };

}  // namespace timetable::domain
