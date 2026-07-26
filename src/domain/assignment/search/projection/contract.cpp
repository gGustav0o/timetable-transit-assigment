#include "timetable/domain/assignment/search/projection/contract.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search_time_domain.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] bool completion_target_projection_slots(
        std::span<const SearchProjectionSlot> slots
    ) noexcept {
        return std::all_of(
              slots.begin()
            , slots.end()
            , [](const SearchProjectionSlot& slot) {
                  return slot.kind == SearchProjectionSlotKind::CompletionTarget;
              }
        );
    }

    [[nodiscard]] bool od_day_projection_slots(
        std::span<const SearchProjectionSlot> slots
    ) noexcept {
        return std::all_of(
              slots.begin()
            , slots.end()
            , [](const SearchProjectionSlot& slot) {
                  return slot.kind == SearchProjectionSlotKind::OdDayPair;
              }
        );
    }

    [[nodiscard]] bool has_projection_slot_kind(
          std::span<const SearchProjectionSlot> slots
        , SearchProjectionSlotKind              kind
    ) noexcept {
        return std::any_of(
              slots.begin()
            , slots.end()
            , [kind](const SearchProjectionSlot& slot) {
                  return slot.kind == kind;
              }
        );
    }

    mathfp::Expected<mathfp::Unit> validate_od_day_production_batch_contract(
          const SearchBatch&                batch
        , SearchPartialRetentionScope       partial_retention_scope
        , const SearchPruningExecutionPlan& pruning_execution
        , bool                              has_day_level_supply
    ) {
        const auto slots = std::span<const SearchProjectionSlot>{
              batch.projection_slots.data()
            , batch.projection_slots.size()
        };
        const auto has_od_day_slots = has_projection_slot_kind(
              slots
            , SearchProjectionSlotKind::OdDayPair
        );
        if (!has_od_day_slots) {
            return mathfp::kUnit;
        }
        if (has_day_level_supply) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production search must use connection-segment carrier, not structural day-level graph")
                    .ctx("origin", batch.key.origin.get())
            );
        }
        if (!od_day_projection_slots(slots)) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production batch cannot mix day-path slots with timed projection slots")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
            );
        }
        if (batch.key.interval.has_value()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production batch must cover the whole service day, not one demand interval")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("interval", batch.key.interval->get())
            );
        }
        if (partial_retention_scope != SearchPartialRetentionScope::TreeGlobal) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production search requires tree-global node-local C_y retention")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("partial_retention_scope", std::string(to_string(partial_retention_scope)))
            );
        }
        if (!pruning_execution.exact_enabled
            || !pruning_execution.approximate_enabled
            || !pruning_execution.approximate_policy.has_value()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production search requires connection-tree-level node-local C_y relevance and tolerance retention")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("exact_enabled", pruning_execution.exact_enabled ? "true" : "false")
                    .ctx("approximate_enabled", pruning_execution.approximate_enabled ? "true" : "false")
            );
        }
        if (batch.completion_targets.size() != batch.projection_slots.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production batch must have one completion target per OD path slot")
                    .ctx("origin", batch.key.origin.get())
                    .ctx("completion_targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                    .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
            );
        }
        if (batch.departure_domain.get().windows.empty()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production first-boarding domain must not be empty")
                    .ctx("origin", batch.key.origin.get())
            );
        }
        for (std::size_t slot_pos = 0; slot_pos < batch.projection_slots.size(); ++slot_pos) {
            const auto& slot = batch.projection_slots[slot_pos];
            if (!slot.completion_target.has_value()
                || slot.completion_target->get() != static_cast<std::int64_t>(slot_pos)
                || slot.task.has_value()
                || slot.task_ref.has_value()
                || slot.result_index.has_value()
                || slot.interval.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production slot carries timed/demand projection payload")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("slot_position", static_cast<std::int64_t>(slot_pos))
                );
            }
            if (batch.completion_targets[slot_pos].destination != slot.destination) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production slot destination disagrees with its completion target")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("slot_position", static_cast<std::int64_t>(slot_pos))
                        .ctx("slot_destination", slot.destination.get())
                        .ctx("target_destination", batch.completion_targets[slot_pos].destination.get())
                );
            }
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_od_day_production_batches(
          std::span<const SearchBatch> batches
        , std::size_t                  expected_tree_count
        , SearchDestinationScope       destination_scope
        , std::size_t                  declared_destination_count
    ) {
        if (batches.size() != expected_tree_count) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day production must have exactly one batch/tree per declared origin")
                    .ctx("batches", static_cast<std::int64_t>(batches.size()))
                    .ctx("expected_tree_count", static_cast<std::int64_t>(expected_tree_count))
            );
        }
        for (std::size_t batch_pos = 0; batch_pos < batches.size(); ++batch_pos) {
            const auto& batch = batches[batch_pos];
            const auto slots = std::span<const SearchProjectionSlot>{
                  batch.projection_slots.data()
                , batch.projection_slots.size()
            };
            if (!od_day_projection_slots(slots)) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch contains non-OD-day projection slots")
                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                        .ctx("origin", batch.key.origin.get())
                );
            }
            if (batch.key.interval.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production batch key must not be demand-interval-local")
                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                        .ctx("origin", batch.key.origin.get())
                        .ctx("interval", batch.key.interval->get())
                );
            }
            if (batch.completion_targets.size() != batch.projection_slots.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production completion targets and OD sinks disagree")
                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                        .ctx("origin", batch.key.origin.get())
                        .ctx("targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                        .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                );
            }
            if (destination_scope == SearchDestinationScope::DeclaredZones
                && batch.completion_targets.size() != declared_destination_count) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day production tree must target all declared destination zones")
                        .ctx("batch", static_cast<std::int64_t>(batch_pos))
                        .ctx("origin", batch.key.origin.get())
                        .ctx("targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                        .ctx("declared_destinations", static_cast<std::int64_t>(declared_destination_count))
                );
            }
            for (std::size_t slot_pos = 0; slot_pos < batch.projection_slots.size(); ++slot_pos) {
                const auto& slot = batch.projection_slots[slot_pos];
                if (slot.origin != batch.key.origin) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production slot origin disagrees with tree origin")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("slot", static_cast<std::int64_t>(slot_pos))
                            .ctx("tree_origin", batch.key.origin.get())
                            .ctx("slot_origin", slot.origin.get())
                    );
                }
                if (!slot.completion_target.has_value()
                    || slot.completion_target->get() != static_cast<std::int64_t>(slot_pos)
                    || batch.completion_targets[slot_pos].destination != slot.destination) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day production sink is not aligned with completion target")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                            .ctx("slot", static_cast<std::int64_t>(slot_pos))
                    );
                }
            }
        }
        return mathfp::kUnit;
    }

    [[nodiscard]] std::size_t active_demand_origin_count(
        std::span<const SearchTask> tasks
    ) {
        std::map<ZoneId, mathfp::Unit> origins;
        for (const auto& task : tasks) {
            origins.emplace(task.origin, mathfp::kUnit);
        }
        return origins.size();
    }

    [[nodiscard]] std::size_t search_origin_count(
        std::span<const SearchBatch> batches
    ) {
        std::map<ZoneId, mathfp::Unit> origins;
        for (const auto& batch : batches) {
            origins.emplace(batch.key.origin, mathfp::kUnit);
        }
        return origins.size();
    }

    [[nodiscard]] std::size_t expected_search_tree_count(
          SearchOriginScope           origin_scope
        , std::size_t                 declared_zone_count
        , std::span<const SearchTask> tasks
    ) {
        switch (origin_scope) {
            case SearchOriginScope::ActiveDemandOrigins:
                return active_demand_origin_count(tasks);

            case SearchOriginScope::DeclaredZones:
                return declared_zone_count;
        }
        return declared_zone_count;
    }

    [[nodiscard]] std::int64_t signed_count_delta(
          std::size_t actual
        , std::size_t expected
    ) noexcept {
        return static_cast<std::int64_t>(actual)
             - static_cast<std::int64_t>(expected);
    }

    mathfp::Expected<mathfp::Unit> validate_search_execution_projection_contract(
        const SearchExecutionConfig& config
    ) {
        if (config.max_parallel_batches == 0u) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search execution maxParallelBatches must be positive")
            );
        }
        if (config.estimated_memory_mb_per_parallel_batch == 0u) {
            return mathfp::unexpected(
                mathfp::invalid_arg("search execution estimatedMemoryMbPerParallelBatch must be positive")
            );
        }
        if (config.mode == SearchExecutionMode::IntervalLocal
            && config.result_projection != SearchResultProjection::DemandTasks) {
            return mathfp::unexpected(
                mathfp::invalid_arg("interval-local search supports only demand-task projection")
                    .ctx("result_projection", std::string(to_string(config.result_projection)))
            );
        }
        const auto origin_period_projection =
               config.result_projection == SearchResultProjection::CompletionTargets
            || config.result_projection == SearchResultProjection::OdDayPairs;
        if (origin_period_projection && config.mode != SearchExecutionMode::OriginPeriod) {
            return mathfp::unexpected(
                mathfp::invalid_arg("tree-level result projection requires origin-period search")
                    .ctx("execution_mode", std::string(to_string(config.mode)))
                    .ctx("result_projection", std::string(to_string(config.result_projection)))
            );
        }
        if (config.partial_retention_scope == SearchPartialRetentionScope::TreeGlobal
            && !origin_period_projection) {
            return mathfp::unexpected(
                mathfp::invalid_arg("tree-global partial retention requires a tree-level result projection")
                    .ctx("result_projection", std::string(to_string(config.result_projection)))
            );
        }
        switch (config.formulation) {
            case AssignmentCalculationFormulation::OdDayAssignment:
                if (!is_required_od_day_assignment_profile(config)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("OD-day formulation must use the production OD-day search profile")
                            .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                            .ctx("mode", std::string(to_string(config.mode)))
                            .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                            .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                            .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                            .ctx("result_projection", std::string(to_string(config.result_projection)))
                            .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                            .ctx("max_parallel_batches", static_cast<std::int64_t>(config.max_parallel_batches))
                    );
                }
                break;

            case AssignmentCalculationFormulation::TimedConnectionDiagnostics:
                if (!is_timed_connection_diagnostics_profile(config)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("timed connection search contour is diagnostic-only")
                            .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                            .ctx("mode", std::string(to_string(config.mode)))
                            .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                            .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                            .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                            .ctx("result_projection", std::string(to_string(config.result_projection)))
                            .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                    );
                }
                break;

            case AssignmentCalculationFormulation::AllZoneSearch:
                if (!is_all_zone_search_diagnostics_profile(config)) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("all-zone search contour is diagnostic-only")
                            .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                            .ctx("result_projection", std::string(to_string(config.result_projection)))
                            .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                    );
                }
                break;

            case AssignmentCalculationFormulation::DemandTaskAssignment:
                return mathfp::unexpected(
                    mathfp::invalid_arg("fallback demand-task timed assignment is disabled; use od_day_assignment for production or timed_connection_diagnostics for search-only diagnostics")
                        .ctx("diagnostic_mode", config.diagnostic_mode ? "true" : "false")
                        .ctx("mode", std::string(to_string(config.mode)))
                        .ctx("origin_scope", std::string(to_string(config.origin_scope)))
                        .ctx("time_domain_source", std::string(to_string(config.time_domain_source)))
                        .ctx("destination_scope", std::string(to_string(config.destination_scope)))
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                        .ctx("partial_retention_scope", std::string(to_string(config.partial_retention_scope)))
                );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<SearchTimeDomainSummary> summarize_batch_search_domains(
        std::span<const SearchBatch> batches
    ) {
        std::vector<SearchTimeWindow> windows;
        for (const auto& batch : batches) {
            const auto& departure_domain = batch.departure_domain.get();
            windows.insert(
                  windows.end()
                , departure_domain.windows.begin()
                , departure_domain.windows.end()
            );
        }
        if (windows.empty()) {
            return SearchTimeDomainSummary{};
        }
        MATHFP_TRY_LET(
              SearchTimeDomain
            , domain
            , make_search_time_domain(std::move(windows))
        );
        return summarize(domain);
    }

    mathfp::Expected<mathfp::Unit> validate_search_batch_projection_contract(
          std::span<const SearchBatch> batches
        , SearchExecutionMode          execution_mode
        , SearchResultProjection       result_projection
        , std::span<const SearchTask>  tasks
    ) {
        for (std::size_t batch_pos = 0; batch_pos < batches.size(); ++batch_pos) {
            const auto& batch = batches[batch_pos];
            switch (execution_mode) {
                case SearchExecutionMode::IntervalLocal:
                    if (!batch.key.interval.has_value()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("interval-local search batch has no interval key")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("origin", batch.key.origin.get())
                        );
                    }
                    break;

                case SearchExecutionMode::OriginPeriod:
                    if (batch.key.interval.has_value()) {
                        return mathfp::unexpected(
                            mathfp::internal_error("origin-period search batch unexpectedly has interval key")
                                .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                .ctx("origin", batch.key.origin.get())
                                .ctx("interval", batch.key.interval->get())
                        );
                    }
                    break;
            }

            std::map<ZoneId, mathfp::Unit> target_destinations;
            for (std::size_t target_pos = 0; target_pos < batch.completion_targets.size(); ++target_pos) {
                const auto& target = batch.completion_targets[target_pos];
                if (target.index.get() != static_cast<std::int64_t>(target_pos)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("search completion target index does not match its position")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("target_position", static_cast<std::int64_t>(target_pos))
                            .ctx("target_index", target.index.get())
                    );
                }
                const auto [_, inserted] = target_destinations.emplace(
                      target.destination
                    , mathfp::kUnit
                );
                if (!inserted) {
                    return mathfp::unexpected(
                        mathfp::internal_error("search batch contains duplicate completion target destination")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                            .ctx("destination", target.destination.get())
                    );
                }
            }

            for (std::size_t task_pos = 0; task_pos < batch.projection_slots.size(); ++task_pos) {
                const auto& slot = batch.projection_slots[task_pos];
                if (slot.origin != batch.key.origin) {
                    return mathfp::unexpected(
                        mathfp::internal_error("search projection slot origin does not match tree origin")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("tree_origin", batch.key.origin.get())
                            .ctx("slot_origin", slot.origin.get())
                    );
                }
                if (!target_destinations.contains(slot.destination)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("search projection slot has no matching completion target")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", slot.origin.get())
                            .ctx("destination", slot.destination.get())
                    );
                }

                switch (result_projection) {
                    case SearchResultProjection::DemandTasks: {
                        if (slot.kind != SearchProjectionSlotKind::DemandTask
                            || !slot.task.has_value()
                            || !slot.result_index.has_value()
                            || !slot.task_ref.has_value()) {
                            return mathfp::unexpected(
                                mathfp::internal_error("demand projection slot has invalid task payload")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                            );
                        }
                        if (*slot.result_index >= tasks.size()) {
                            return mathfp::unexpected(
                                mathfp::internal_error("demand projection slot result index is out of range")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                    .ctx("result_index", static_cast<std::int64_t>(*slot.result_index))
                                    .ctx("task_count", static_cast<std::int64_t>(tasks.size()))
                            );
                        }
                        const auto& task = slot.task->get();
                        if (*slot.task_ref != task.index) {
                            return mathfp::unexpected(
                                mathfp::internal_error("search projection slot task ref disagrees with task payload")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("task_position", static_cast<std::int64_t>(task_pos))
                                    .ctx("slot_task", slot.task_ref->get())
                                    .ctx("task", task.index.get())
                            );
                        }
                        if (slot.origin != task.origin || slot.destination != task.destination) {
                            return mathfp::unexpected(
                                mathfp::internal_error("search projection slot OD disagrees with task payload")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("task", task.index.get())
                            );
                        }
                        if (execution_mode == SearchExecutionMode::IntervalLocal
                            && task.interval.id != *batch.key.interval) {
                            return mathfp::unexpected(
                                mathfp::internal_error("interval-local projection task interval does not match batch interval")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("task", task.index.get())
                                    .ctx("batch_interval", batch.key.interval->get())
                                    .ctx("task_interval", task.interval.id.get())
                            );
                        }
                        break;
                    }

                    case SearchResultProjection::OdDayPairs:
                        if (slot.kind != SearchProjectionSlotKind::OdDayPair
                            || !slot.completion_target.has_value()
                            || slot.task.has_value()
                            || slot.task_ref.has_value()
                            || slot.result_index.has_value()
                            || slot.interval.has_value()) {
                            return mathfp::unexpected(
                                mathfp::internal_error("OD-day projection slot has invalid payload")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                            );
                        }
                        if (slot.completion_target->get()
                            != static_cast<std::int64_t>(task_pos)) {
                            return mathfp::unexpected(
                                mathfp::internal_error("OD-day projection slot index does not match its position")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                    .ctx("target_index", slot.completion_target->get())
                            );
                        }
                        if (batch.completion_targets[task_pos].destination
                            != slot.destination) {
                            return mathfp::unexpected(
                                mathfp::internal_error("OD-day projection slot destination disagrees with target position")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                    .ctx("slot_destination", slot.destination.get())
                                    .ctx("target_destination", batch.completion_targets[task_pos].destination.get())
                            );
                        }
                        break;

                    case SearchResultProjection::CompletionTargets:
                        if (slot.kind != SearchProjectionSlotKind::CompletionTarget
                            || !slot.completion_target.has_value()
                            || slot.task.has_value()
                            || slot.task_ref.has_value()
                            || slot.result_index.has_value()) {
                            return mathfp::unexpected(
                                mathfp::internal_error("completion-target projection slot has invalid payload")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                            );
                        }
                        if (slot.completion_target->get()
                            != static_cast<std::int64_t>(task_pos)) {
                            return mathfp::unexpected(
                                mathfp::internal_error("completion-target projection slot index does not match its position")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                    .ctx("target_index", slot.completion_target->get())
                            );
                        }
                        if (batch.completion_targets[task_pos].destination
                            != slot.destination) {
                            return mathfp::unexpected(
                                mathfp::internal_error("completion-target projection slot destination disagrees with target position")
                                    .ctx("batch", static_cast<std::int64_t>(batch_pos))
                                    .ctx("slot_position", static_cast<std::int64_t>(task_pos))
                                    .ctx("slot_destination", slot.destination.get())
                                    .ctx("target_destination", batch.completion_targets[task_pos].destination.get())
                            );
                        }
                        break;
                }
            }
        }

        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment
