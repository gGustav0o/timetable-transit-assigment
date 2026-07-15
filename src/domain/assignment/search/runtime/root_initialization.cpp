#include "timetable/domain/assignment/search/runtime/root_initialization.hpp"

#include <optional>
#include <span>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"
#include "timetable/domain/assignment/search/runtime/reachability_masks.hpp"
#include "timetable/domain/assignment/search/tree/level_expansion.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        [[nodiscard]] SearchBranch make_root_branch(ZoneId origin) {
            return SearchBranch{
                  .trace = SearchPartialTrace{
                        .origin                   = origin
                      , .current_physical         = endpoint_key(origin)
                      , .current_occurrence       = std::nullopt
                      , .phase                    = SearchBranchPhase::AtOrigin
                      , .parent_branch            = std::nullopt
                      , .incoming_segment         = std::nullopt
                      , .last_timed_segment       = nullptr
                      , .last_timed_route_segment = nullptr
                  }
                , .metrics = SearchPartialMetrics{
                        .departure        = std::nullopt
                      , .current_time     = std::nullopt
                      , .access_time      = Time{ 0.0 }
                      , .in_vehicle_time  = Time{ 0.0 }
                      , .transfer_wait_time = Time{ 0.0 }
                      , .transfer_walk_time = Time{ 0.0 }
                      , .egress_time      = Time{ 0.0 }
                      , .transfers        = TransferCount{ 0 }
                      , .fare             = 0.0
                      , .capacity_exposure = CapacityExposure{ Time{ 0.0 } }
                }
                , .od_day_carrier = OdDayProductionCarrier{
                      .path_identity = make_od_day_path_prefix(origin)
                  }
            };
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit>
        initialize_completion_root_projection(
              SearchBatchContext& context
            , const SearchBranch& root
        ) {
            const auto& fixed = context.fixed;
            auto& state = context.mutable_state;

            const auto root_reachability_key = reachability_mask_key(
                  root
                , fixed.params.transfers
            );
            auto root_reachable_targets = filter_target_positions_by_reachability(
                  ActiveIndexSet::full(fixed.batch.completion_targets.size())
                , state.reachability_cache->target_entry(root_reachability_key)
            );
            auto root_reachability = filter_task_positions_by_reachability(
                  ActiveIndexSet::full(fixed.batch.projection_slots.size())
                , state.reachability_cache->slot_entry(root_reachability_key)
            );
            record_reachability_rejections(
                  std::span<const RejectedReachabilityTask>{
                      root_reachability.unreachable.data()
                    , root_reachability.unreachable.size()
                  }
                , state.task_stats
                , state.stats
            );
            if (!root_reachability.reachable.equals(root_reachable_targets)) {
                return mathfp::unexpected(
                    mathfp::internal_error("completion-target root reachability masks disagree")
                        .ctx("origin", fixed.batch.key.origin.get())
                );
            }
            state.completion_projection_states.push_back(
                FixedActiveMask::from(root_reachability.reachable)
            );
            return mathfp::kUnit;
        }

        void initialize_demand_root_projection(
              SearchBatchContext& context
            , const SearchBranch& root
        ) {
            const auto& fixed = context.fixed;
            auto& state = context.mutable_state;

            const auto root_reachability_key = reachability_mask_key(
                  root
                , fixed.params.transfers
            );
            auto root_reachable_targets = filter_target_positions_by_reachability(
                  ActiveIndexSet::full(fixed.batch.completion_targets.size())
                , state.reachability_cache->target_entry(root_reachability_key)
            );
            auto root_reachability = filter_task_positions_by_reachability(
                  ActiveIndexSet::full(fixed.batch.projection_slots.size())
                , state.reachability_cache->slot_entry(root_reachability_key)
            );
            record_reachability_rejections(
                  std::span<const RejectedReachabilityTask>{
                      root_reachability.unreachable.data()
                    , root_reachability.unreachable.size()
                  }
                , state.task_stats
                , state.stats
            );
            state.demand_projection_states.push_back(
                DemandBranchProjectionState{
                      .active_tasks = std::move(root_reachability.reachable)
                    , .active_targets = std::move(root_reachable_targets)
                }
            );
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> initialize_search_batch_root(
        SearchBatchContext& context
    ) {
        const auto& fixed = context.fixed;
        auto& state = context.mutable_state;

        const auto root_branch_index = append_branch(
              state.branches
            , make_root_branch(fixed.batch.key.origin)
        );
        const auto& root = branch_at(state.branches, root_branch_index);

        if (fixed.diagnostics.validate_phase_invariants) {
            MATHFP_TRY(validate_search_branch_phase_invariants(root));
        }
        if (!fixed.od_day_slots && fixed.target_projection_slots) {
            MATHFP_TRY(initialize_completion_root_projection(context, root));
        } else if (!fixed.od_day_slots) {
            initialize_demand_root_projection(context, root);
        }
        seed_search_level_expansion(
              state.level_expansion
            , root_branch_index
            , SearchBranchPhase::AtOrigin
        );
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::runtime
