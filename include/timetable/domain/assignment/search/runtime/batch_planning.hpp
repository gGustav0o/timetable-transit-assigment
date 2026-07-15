#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/execution_request.hpp"
#include "timetable/domain/assignment/search/diagnostics.hpp"
#include "timetable/domain/assignment/search/problem.hpp"
#include "timetable/domain/assignment/search/projection/contract.hpp"

namespace timetable::domain::assignment::runtime::detail {

    struct SearchBatchPlanningOptions final {
        bool summarize_time_domains{};
    };

    struct SearchBatchPlan final {
        std::vector<SearchTreeJob> tree_jobs{};
        std::vector<SearchBatch> batches{};
        SearchBatchExecutionDiagnostics batch_diagnostics{};
        std::size_t expected_tree_count{};
        std::optional<SearchTimeDomainSummary> time_domain_summary{};
    };

    [[nodiscard]] mathfp::Expected<SearchBatchPlan> plan_search_batches(
          std::span<const SearchTask>   tasks
        , const SearchExecutionRequest& execution
        , SearchDiagnosticsContext      diagnostics
        , SearchBatchPlanningOptions    options = {}
    );

}  // namespace timetable::domain::assignment::runtime::detail
