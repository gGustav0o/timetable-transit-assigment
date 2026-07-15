#include "timetable/domain/assignment/search/runtime/result_finalization.hpp"

#include <utility>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/search/projection/complete_connection.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        [[nodiscard]] std::size_t retained_before_tolerance(
              const SearchProjectionSlot&      slot
            , const SearchProjectionRetention& retention
            , bool                             target_projection_slots
        ) noexcept {
            if (target_projection_slots) {
                return retention.compact_complete_connections.metrics.size();
            }
            if (slot.kind == SearchProjectionSlotKind::OdDayPair) {
                return day_path_retention_size(retention.day_paths);
            }
            return retention.complete_connections.alternatives.size();
        }

        void add_projection_finalization_stats(
              const TaskSearchStats& task_stats
            , TaskSearchStats&       stats
        ) noexcept {
            stats.rejected_complete_admissibility +=
                task_stats.rejected_complete_admissibility;
            stats.rejected_complete_dominance +=
                task_stats.rejected_complete_dominance;
            stats.removed_complete_dominated +=
                task_stats.removed_complete_dominated;
            stats.rejected_complete_tolerance +=
                task_stats.rejected_complete_tolerance;
        }

    }  // namespace

    mathfp::Expected<SearchBatchFinalization>
    finalize_search_batch_results(
          std::span<const SearchProjectionSlot> projection_slots
        , std::span<SearchProjectionRetention>  retentions
        , const ChoiceTolerances&               choice_tolerances
        , ChoiceRolloutStage                    choice_rollout_stage
        , bool                                  target_projection_slots
        , std::vector<TaskSearchStats>&         task_stats
        , TaskSearchStats&                      stats
    ) {
        SearchBatchFinalization finalization;
        finalization.slot_results.reserve(projection_slots.size());
        finalization.slot_finalizations.reserve(projection_slots.size());

        for (std::size_t task_pos = 0; task_pos < projection_slots.size(); ++task_pos) {
            const auto& slot = projection_slots[task_pos];
            auto& retention = retentions[task_pos];
            MATHFP_TRY(validate_od_day_post_layer_retention(slot, retention));

            const auto before_tolerance = retained_before_tolerance(
                  slot
                , retention
                , target_projection_slots
            );
            std::vector<SearchConnection> connections;
            std::vector<DayPathAlternative> day_path_alternatives;
            auto connection_count = std::size_t{ 0u };

            if (target_projection_slots) {
                connection_count = finalize_compact_complete_connection_count(
                      retention.compact_complete_connections
                    , choice_tolerances
                    , choice_rollout_stage
                );
            } else if (slot.kind == SearchProjectionSlotKind::OdDayPair) {
                day_path_alternatives = finalize_day_path_alternatives(
                    std::move(retention.day_paths)
                );
                for (std::size_t i = 0; i < day_path_alternatives.size(); ++i) {
                    MATHFP_TRY(validate_day_path_alternative(
                          day_path_alternatives[i]
                        , i
                    ));
                }
                connection_count = day_path_alternatives.size();
            } else {
                connections = finalize_complete_connection_retention(
                      retention.complete_connections
                    , choice_tolerances
                    , choice_rollout_stage
                );
                connection_count = connections.size();
            }

            task_stats[task_pos].rejected_complete_tolerance =
                before_tolerance - connection_count;
            add_projection_finalization_stats(task_stats[task_pos], stats);

            finalization.final_found += connection_count;
            finalization.retained_before_tolerance += before_tolerance;
            finalization.slot_finalizations.push_back(
                SearchSlotFinalization{
                    .retained_before_tolerance = before_tolerance
                }
            );
            finalization.slot_results.push_back(
                SearchSlotResult{
                      .slot = slot
                    , .connection_count = connection_count
                    , .connections = std::move(connections)
                    , .day_path_alternatives = std::move(day_path_alternatives)
                }
            );

            MATHFP_TRY(validate_od_day_post_layer_result(
                finalization.slot_results.back()
            ));
            MATHFP_TRY(validate_reachability_rejection_stats(task_stats[task_pos]));
            MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(task_stats[task_pos]));
        }

        MATHFP_TRY(validate_reachability_rejection_stats(stats));
        MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(stats));

        return finalization;
    }

}  // namespace timetable::domain::assignment::runtime
