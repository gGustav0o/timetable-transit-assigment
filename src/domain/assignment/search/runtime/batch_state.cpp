#include "timetable/domain/assignment/search/runtime/batch_state.hpp"

#include <cstdint>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/projection/sink.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        [[nodiscard]] std::map<ZoneId, std::size_t>
        completion_target_position_by_destination(
            std::span<const SearchCompletionTarget> batch_targets
        ) {
            std::map<ZoneId, std::size_t> positions;
            for (std::size_t target_pos = 0; target_pos < batch_targets.size(); ++target_pos) {
                positions[batch_targets[target_pos].destination] = target_pos;
            }
            return positions;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit>
        validate_completion_target_projection_state(
              const SearchBatch& batch
            , bool               target_projection_slots
        ) {
            if (!target_projection_slots) {
                return mathfp::kUnit;
            }
            if (batch.projection_slots.size() > FixedActiveMask::max_size) {
                return mathfp::unexpected(
                    mathfp::internal_error("completion-target fixed active mask capacity exceeded")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "projection_slots"
                            , static_cast<std::int64_t>(batch.projection_slots.size())
                          )
                        .ctx(
                              "mask_capacity"
                            , static_cast<std::int64_t>(FixedActiveMask::max_size)
                          )
                );
            }
            if (batch.projection_slots.size() != batch.completion_targets.size()) {
                return mathfp::unexpected(
                    mathfp::internal_error("completion-target projection size disagrees with target count")
                        .ctx("origin", batch.key.origin.get())
                        .ctx(
                              "projection_slots"
                            , static_cast<std::int64_t>(batch.projection_slots.size())
                          )
                        .ctx(
                              "completion_targets"
                            , static_cast<std::int64_t>(batch.completion_targets.size())
                          )
                );
            }
            for (std::size_t target_pos = 0; target_pos < batch.projection_slots.size(); ++target_pos) {
                const auto& slot = batch.projection_slots[target_pos];
                const auto& target = batch.completion_targets[target_pos];
                if (!slot.completion_target.has_value()
                    || slot.completion_target->get()
                        != static_cast<std::int64_t>(target_pos)
                    || slot.destination != target.destination) {
                    return mathfp::unexpected(
                        mathfp::internal_error("completion-target projection slot cannot share reachability mask with target")
                            .ctx("origin", batch.key.origin.get())
                            .ctx("position", static_cast<std::int64_t>(target_pos))
                            .ctx("slot_destination", slot.destination.get())
                            .ctx("target_destination", target.destination.get())
                    );
                }
            }
            return mathfp::kUnit;
        }

    }  // namespace

    SearchBatchState::SearchBatchState(SearchBatchState&& other) noexcept
        : batch_tasks{ other.batch_tasks }
        , batch_targets{ other.batch_targets }
        , projection_sinks{ std::move(other.projection_sinks) }
        , retentions{ std::move(other.retentions) }
        , tree_partial_retention{ std::move(other.tree_partial_retention) }
        , paper_label_registry{ std::move(other.paper_label_registry) }
        , stats{ std::move(other.stats) }
        , task_stats{ std::move(other.task_stats) }
        , branches{ std::move(other.branches) }
        , completion_projection_states{
              std::move(other.completion_projection_states)
          }
        , demand_projection_states{ std::move(other.demand_projection_states) }
        , released_branches{ other.released_branches }
        , first_departure_domain{ other.first_departure_domain }
        , target_projection_slots{ other.target_projection_slots }
        , od_day_slots{ other.od_day_slots }
        , max_transfers{ other.max_transfers }
        , od_day_supply{ other.od_day_supply }
        , target_positions_by_destination{
              std::move(other.target_positions_by_destination)
          }
        , od_day_destination_ids{ std::move(other.od_day_destination_ids) }
        , reachability{ std::move(other.reachability) }
        , level_expansion{ std::move(other.level_expansion) }
    {
        if (other.reachability_cache.has_value() && reachability.has_value()) {
            reachability_cache.emplace(
                ReachabilityMaskCache{
                      .reachability = *reachability
                    , .targets = batch_targets
                    , .slots = batch_tasks
                    , .max_transfers = max_transfers
                    , .unified_completion_targets = target_projection_slots
                }
            );
        }
    }

    SearchBatchState& SearchBatchState::operator=(
        SearchBatchState&& other
    ) noexcept {
        if (this == &other) {
            return *this;
        }
        batch_tasks = other.batch_tasks;
        batch_targets = other.batch_targets;
        projection_sinks = std::move(other.projection_sinks);
        retentions = std::move(other.retentions);
        tree_partial_retention = std::move(other.tree_partial_retention);
        paper_label_registry = std::move(other.paper_label_registry);
        stats = std::move(other.stats);
        task_stats = std::move(other.task_stats);
        branches = std::move(other.branches);
        completion_projection_states =
            std::move(other.completion_projection_states);
        demand_projection_states =
            std::move(other.demand_projection_states);
        released_branches = other.released_branches;
        first_departure_domain = other.first_departure_domain;
        target_projection_slots = other.target_projection_slots;
        od_day_slots = other.od_day_slots;
        max_transfers = other.max_transfers;
        od_day_supply = other.od_day_supply;
        target_positions_by_destination =
            std::move(other.target_positions_by_destination);
        od_day_destination_ids = std::move(other.od_day_destination_ids);
        reachability = std::move(other.reachability);
        reachability_cache.reset();
        if (other.reachability_cache.has_value() && reachability.has_value()) {
            reachability_cache.emplace(
                ReachabilityMaskCache{
                      .reachability = *reachability
                    , .targets = batch_targets
                    , .slots = batch_tasks
                    , .max_transfers = max_transfers
                    , .unified_completion_targets = target_projection_slots
                }
            );
        }
        level_expansion = std::move(other.level_expansion);
        return *this;
    }

    SearchBatchContext SearchBatchState::context(
        const SearchBatchEnvironment& environment
    ) {
        return SearchBatchContext{
              .fixed = SearchBatchStaticContext{
                  .batch = environment.batch
                , .network = environment.network
                , .params = environment.params
                , .search_cost = environment.search_cost
                , .choice_config = environment.choice_config
                , .assignment_period = environment.assignment_period
                , .admissibility_config = environment.admissibility_config
                , .pruning_execution = environment.pruning_execution
                , .complete_connection_dominance =
                      environment.complete_connection_dominance
                , .partial_retention_scope = environment.partial_retention_scope
                , .diagnostics = environment.diagnostics
                , .batch_index = environment.batch_index
                , .batch_count = environment.batch_count
                , .od_day_supply = od_day_supply
                , .cancellation = environment.cancellation
                , .first_departure_domain = first_departure_domain
                , .batch_tasks = batch_tasks
                , .batch_targets = batch_targets
                , .target_projection_slots = target_projection_slots
                , .od_day_slots = od_day_slots
                , .target_positions_by_destination =
                      &target_positions_by_destination
                , .projection_positions_by_destination =
                      &projection_sinks.position_by_destination
                , .od_day_destination_ids = &od_day_destination_ids
              }
            , .mutable_state = SearchBatchMutableState{
                  .retentions = retentions
                , .tree_partial_retention = tree_partial_retention
                , .paper_label_registry = paper_label_registry
                , .stats = stats
                , .task_stats = task_stats
                , .branches = branches
                , .completion_projection_states =
                      completion_projection_states
                , .demand_projection_states = demand_projection_states
                , .released_branches = released_branches
                , .reachability = reachability
                , .reachability_cache = reachability_cache
                , .level_expansion = level_expansion
              }
        };
    }

    mathfp::Expected<SearchBatchState> prepare_search_batch_state(
        const SearchBatchEnvironment& environment
    ) {
        const auto& batch = environment.batch;
        SearchBatchState state;
        state.batch_tasks = std::span<const SearchProjectionSlot>{
              batch.projection_slots.data()
            , batch.projection_slots.size()
        };
        state.batch_targets = std::span<const SearchCompletionTarget>{
              batch.completion_targets.data()
            , batch.completion_targets.size()
        };
        state.projection_sinks = make_search_projection_sink_set(
            state.batch_tasks
        );
        state.retentions = make_search_projection_retentions(
            state.projection_sinks
        );
        state.task_stats.resize(batch.projection_slots.size());
        state.first_departure_domain = &batch.departure_domain.get();
        state.target_projection_slots =
            state.projection_sinks.completion_target_slots;
        state.od_day_slots = state.projection_sinks.od_day_slots;
        state.max_transfers = environment.params.transfers.max_transfers;
        state.od_day_supply =
            state.od_day_slots ? nullptr : environment.day_level_supply;
        state.target_positions_by_destination =
            state.target_projection_slots
                ? completion_target_position_by_destination(state.batch_targets)
                : std::map<ZoneId, std::size_t>{};

        if (state.od_day_slots) {
            state.od_day_destination_ids.reserve(batch.completion_targets.size());
            for (const auto& target : batch.completion_targets) {
                state.od_day_destination_ids.insert(target.destination.get());
            }
        }

        MATHFP_TRY(validate_completion_target_projection_state(
              batch
            , state.target_projection_slots
        ));

        if (!state.od_day_slots) {
            state.reachability.emplace(build_residual_reachability(
                  environment.reverse_graph
                , state.batch_targets
                , environment.params.transfers.max_transfers
                , environment.search_cost.impedance
                , environment.search_cost.fare_scale
            ));
            MATHFP_TRY(validate_residual_reachability(
                  *state.reachability
                , environment.params.transfers.max_transfers
            ));
            state.reachability_cache.emplace(
                ReachabilityMaskCache{
                      .reachability = *state.reachability
                    , .targets = state.batch_targets
                    , .slots = state.batch_tasks
                    , .max_transfers = state.max_transfers
                    , .unified_completion_targets =
                          state.target_projection_slots
                }
            );
        }

        return state;
    }

}  // namespace timetable::domain::assignment::runtime
