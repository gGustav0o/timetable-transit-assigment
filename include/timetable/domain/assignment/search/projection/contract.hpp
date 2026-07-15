#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/demand.hpp"
#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain_diagnostics.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] bool completion_target_projection_slots(
        std::span<const SearchProjectionSlot> slots
    ) noexcept;

    [[nodiscard]] bool od_day_projection_slots(
        std::span<const SearchProjectionSlot> slots
    ) noexcept;

    [[nodiscard]] bool has_projection_slot_kind(
          std::span<const SearchProjectionSlot> slots
        , SearchProjectionSlotKind              kind
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_od_day_production_batch_contract(
          const SearchBatch&                batch
        , SearchPartialRetentionScope       partial_retention_scope
        , const SearchPruningExecutionPlan& pruning_execution
        , bool                              has_day_level_supply
    );

    mathfp::Expected<mathfp::Unit> validate_od_day_production_batches(
          std::span<const SearchBatch> batches
        , std::size_t                  expected_tree_count
        , SearchDestinationScope       destination_scope
        , std::size_t                  declared_destination_count
    );

    [[nodiscard]] std::size_t active_demand_origin_count(
        std::span<const SearchTask> tasks
    );

    [[nodiscard]] std::size_t search_origin_count(
        std::span<const SearchBatch> batches
    );

    [[nodiscard]] std::size_t expected_search_tree_count(
          SearchOriginScope           origin_scope
        , std::size_t                 declared_zone_count
        , std::span<const SearchTask> tasks
    );

    [[nodiscard]] std::int64_t signed_count_delta(
          std::size_t actual
        , std::size_t expected
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_search_execution_projection_contract(
        const SearchExecutionConfig& config
    );

    mathfp::Expected<SearchTimeDomainSummary> summarize_batch_search_domains(
        std::span<const SearchBatch> batches
    );

    mathfp::Expected<mathfp::Unit> validate_search_batch_projection_contract(
          std::span<const SearchBatch> batches
        , SearchExecutionMode          execution_mode
        , SearchResultProjection       result_projection
        , std::span<const SearchTask>  tasks
    );

}  // namespace timetable::domain::assignment
