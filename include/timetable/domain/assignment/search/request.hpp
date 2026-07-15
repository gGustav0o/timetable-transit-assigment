#pragma once

#include <functional>
#include <optional>
#include <span>

#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/diagnostics.hpp"
#include "timetable/domain/assignment/search/execution_request.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Application-level execution request for branch-and-bound search.
     *
     * This is not the mathematical kernel input. Runtime code must lower this
     * request into SearchProblem + SearchPolicy values before invoking the
     * branch-and-bound kernel. Choice, assignment-period, admissibility,
     * projection and diagnostics fields belong to orchestration layers.
     */
    struct BranchAndBoundSearchRequest final {
        const PreprocessedNetwork&          network;
        std::span<const SearchTask>          tasks;
        SearchExecutionRequest              execution;
        const SearchParams&                 params;
        const SearchCostContext&            search_cost;
        const ChoiceConfig&                 choice_config;
        const AssignmentPeriodConfig&       assignment_period;
        const ConnectionAdmissibilityConfig& admissibility_config;
        std::optional<std::reference_wrapper<const SearchPruningExecutionPlan>>
            pruning_execution{};
        CompleteConnectionDominanceConfig   complete_connection_dominance{};
        SearchDiagnosticsContext            diagnostics{};
    };

}  // namespace timetable::domain::assignment
