#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment::runtime {

    struct SearchSlotFinalization final {
        std::size_t retained_before_tolerance{};
    };

    struct SearchBatchFinalization final {
        std::vector<SearchSlotResult>       slot_results{};
        std::vector<SearchSlotFinalization> slot_finalizations{};
        std::size_t                         final_found{};
        std::size_t                         retained_before_tolerance{};
    };

    [[nodiscard]] mathfp::Expected<SearchBatchFinalization>
    finalize_search_batch_results(
          std::span<const SearchProjectionSlot> projection_slots
        , std::span<SearchProjectionRetention>  retentions
        , const ChoiceTolerances&               choice_tolerances
        , ChoiceRolloutStage                    choice_rollout_stage
        , bool                                  target_projection_slots
        , std::vector<TaskSearchStats>&         task_stats
        , TaskSearchStats&                      stats
    );

}  // namespace timetable::domain::assignment::runtime
