#include "timetable/domain/assignment/search/runtime/batch_runner.hpp"

#include <span>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/projection/contract.hpp"
#include "timetable/domain/assignment/search/runtime/batch_diagnostics.hpp"
#include "timetable/domain/assignment/search/runtime/batch_state.hpp"
#include "timetable/domain/assignment/search/runtime/batch_tree_execution.hpp"
#include "timetable/domain/assignment/search/runtime/result_finalization.hpp"
#include "timetable/domain/assignment/search/runtime/result_materialization.hpp"
#include "timetable/domain/assignment/search/runtime/root_initialization.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::runtime::detail {

    mathfp::Expected<std::vector<SearchSlotResult>> search_batch_connections(
          const SearchBatch&                 batch
        , const PreprocessedNetwork&         network
        , const ResidualReverseGraph&        reverse_graph
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan&  pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchPartialRetentionScope        partial_retention_scope
        , SearchDiagnosticsContext           diagnostics
        , std::size_t                        batch_index
        , std::size_t                        batch_count
        , const DayLevelSupplySearchGraph*   day_level_supply
        , const SearchCancellationToken*     cancellation
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        if (batch.completion_targets.empty()) {
            log(
                fmt::format(
                      "search batch skipped: origin={} interval={} has no completion targets"
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                )
                , LogLevel::Info
            );
            return std::vector<SearchSlotResult>{};
        }
        if (batch.projection_slots.empty()) {
            log(
                fmt::format(
                      "search batch skipped: origin={} interval={} has no projection tasks"
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                )
                , LogLevel::Info
            );
            return std::vector<SearchSlotResult>{};
        }
        MATHFP_TRY(validate_od_day_production_batch_contract(
              batch
            , partial_retention_scope
            , pruning_execution
            , day_level_supply != nullptr
        ));

        const SearchBatchEnvironment batch_environment{
              .batch = batch
            , .network = network
            , .reverse_graph = reverse_graph
            , .params = params
            , .search_cost = search_cost
            , .choice_config = choice_config
            , .assignment_period = assignment_period
            , .admissibility_config = admissibility_config
            , .pruning_execution = pruning_execution
            , .complete_connection_dominance =
                  complete_connection_dominance
            , .partial_retention_scope = partial_retention_scope
            , .diagnostics = diagnostics
            , .batch_index = batch_index
            , .batch_count = batch_count
            , .day_level_supply = day_level_supply
            , .cancellation = cancellation
        };
        MATHFP_TRY_LET(
              SearchBatchState
            , batch_state
            , prepare_search_batch_state(batch_environment)
        );
        auto batch_context = batch_state.context(batch_environment);

        auto& retentions = batch_state.retentions;
        auto& tree_partial_retention = batch_state.tree_partial_retention;
        auto& paper_label_registry = batch_state.paper_label_registry;
        auto& stats = batch_state.stats;
        auto& task_stats = batch_state.task_stats;
        const auto target_projection_slots =
            batch_state.target_projection_slots;
        const auto od_day_slots = batch_state.od_day_slots;
        MATHFP_TRY(initialize_search_batch_root(batch_context));
        SearchBatchDiagnosticsRuntime batch_diagnostics{ batch_context };
        batch_diagnostics.emit_initial_status();
        const OdDayProductionMemoryLimits od_day_memory_limits{};
        batch_diagnostics.emit_od_day_memory_limits(od_day_memory_limits);

        MATHFP_TRY_LET(
              SearchTreeRunStatus
            , tree_run_status
            , run_search_batch_tree(
                  batch_context
                , batch_diagnostics
                , od_day_memory_limits
            )
        );
        if (tree_run_status == SearchTreeRunStatus::Stopped) {
            return std::vector<SearchSlotResult>{};
        }

        if (od_day_slots) {
            stats.c_y_removed_stale += remove_inactive_paper_connection_metrics(
                  tree_partial_retention.paper_connections
                , paper_label_registry
            );
            MATHFP_TRY(validate_paper_connection_label_sync(
                  tree_partial_retention.paper_connections
                , paper_label_registry
                , batch.key.origin
            ));
        }

        MATHFP_TRY_LET(
              SearchBatchFinalization
            , finalization
            , finalize_search_batch_results(
                  std::span<const SearchProjectionSlot>{
                      batch.projection_slots.data()
                    , batch.projection_slots.size()
                  }
                , std::span<SearchProjectionRetention>{
                      retentions.data()
                    , retentions.size()
                  }
                , params.choice_tolerances
                , choice_config.rollout_stage
                , target_projection_slots
                , task_stats
                , stats
            )
        );
        batch_diagnostics.emit_projection_details(finalization);
        batch_diagnostics.emit_batch_done(finalization);
        if (od_day_slots) {
            MATHFP_TRY(validate_od_day_production_batch_invariants(
                  batch
                , stats
                , batch_diagnostics.storage_diagnostics()
            ));
        }

        return finalization.slot_results;
    }

}  // namespace timetable::domain::assignment::runtime::detail
