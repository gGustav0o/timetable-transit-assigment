#pragma once

#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/capacity_aware_assignment.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/execution_config.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_pruning_config.hpp"
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
     * CompleteConnectionDominanceConfig controls dominance between complete
     * alternatives and is intentionally separate from partial search pruning.
     * Connection deletion and demand-segment time configs are kept outside
     * SearchParams because they decide admissibility of alternatives for
     * choice/split rather than search impedance or split weights.
     * AssignmentExecutionConfig controls top-level execution stages and is not
     * part of the mathematical search/choice/split model.
     * CapacityAwareAssignmentConfig controls the endogenous behavioral load
     * layer and is separate from post-assignment overload assessment.
     */
    struct AssignmentRuntimeParams final {
        SearchParams                         search{};
        assignment::AssignmentExecutionConfig execution{};
        assignment::CompleteConnectionDominanceConfig complete_connection_dominance{};
        assignment::SearchExecutionConfig    search_execution{};
        assignment::SearchPruningConfig      search_pruning{};
        assignment::SkimMatrixConfig         skim_matrix{};
        assignment::AssignmentPeriodConfig   assignment_period{};
        assignment::ConnectionDeletionConfig connection_deletion{};
        assignment::DemandSegmentTimeConfig  demand_segment_time{};
        assignment::CapacityAwareAssignmentConfig capacity_aware_assignment{};
    };

}  // namespace timetable::domain
