#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/runtime/batch_context.hpp"

namespace timetable::domain::assignment::runtime {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> initialize_search_batch_root(
        SearchBatchContext& context
    );

}  // namespace timetable::domain::assignment::runtime
