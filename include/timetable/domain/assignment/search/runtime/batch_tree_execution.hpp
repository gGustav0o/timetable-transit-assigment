#pragma once

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/search/runtime/batch_context.hpp"
#include "timetable/domain/assignment/search/runtime/batch_diagnostics.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/assignment/search/tree/tree_runner.hpp"

namespace timetable::domain::assignment::runtime {

    [[nodiscard]] mathfp::Expected<SearchTreeRunStatus> run_search_batch_tree(
          SearchBatchContext&             context
        , SearchBatchDiagnosticsRuntime&  diagnostics
        , const OdDayProductionMemoryLimits& od_day_memory_limits
    );

}  // namespace timetable::domain::assignment::runtime
