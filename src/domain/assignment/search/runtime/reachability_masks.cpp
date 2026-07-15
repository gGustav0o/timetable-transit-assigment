#include "timetable/domain/assignment/search/runtime/reachability_masks.hpp"

#include <algorithm>

#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
              const ResidualReachabilityKey& key
            , ZoneId                         destination
        ) noexcept {
            return RelaxedSuffixState{
                  .current_physical    = key.current_physical
                , .destination         = destination
                , .phase               = key.phase
                , .remaining_transfers = key.remaining_transfers
            };
        }

        [[nodiscard]] ReachabilityMaskEntry build_target_reachability_mask(
              const ResidualReachabilityKey&          key
            , std::span<const SearchCompletionTarget> targets
            , const ResidualReachability&             reachability
            , TransferCount                           max_transfers
        ) {
            ReachabilityMaskEntry result{
                  .reachable = ActiveIndexSet{ targets.size() }
                , .rejection_reasons = CompactReachabilityReasons{
                      targets.size()
                    , ReachabilityRejectionReason::UnreachableDestination
                  }
            };
            for (std::size_t target_pos = 0; target_pos < targets.size(); ++target_pos) {
                const auto decision = evaluate_residual_reachability(
                      reachability
                    , relaxed_suffix_state(key, targets[target_pos].destination)
                    , max_transfers
                );
                if (decision.feasible) {
                    result.reachable.set(target_pos);
                } else {
                    result.rejection_reasons.set(
                          target_pos
                        , decision.rejection_reason
                    );
                }
            }
            return result;
        }

        [[nodiscard]] ReachabilityMaskEntry build_slot_reachability_mask(
              const ResidualReachabilityKey&       key
            , std::span<const SearchProjectionSlot> slots
            , const ResidualReachability&          reachability
            , TransferCount                        max_transfers
        ) {
            ReachabilityMaskEntry result{
                  .reachable = ActiveIndexSet{ slots.size() }
                , .rejection_reasons = CompactReachabilityReasons{
                      slots.size()
                    , ReachabilityRejectionReason::UnreachableDestination
                  }
            };
            for (std::size_t task_pos = 0; task_pos < slots.size(); ++task_pos) {
                const auto decision = evaluate_residual_reachability(
                      reachability
                    , relaxed_suffix_state(key, slots[task_pos].destination)
                    , max_transfers
                );
                if (decision.feasible) {
                    result.reachable.set(task_pos);
                } else {
                    result.rejection_reasons.set(
                          task_pos
                        , decision.rejection_reason
                    );
                }
            }
            return result;
        }

    }  // namespace

    CompactReachabilityReasons::CompactReachabilityReasons(
          std::size_t                 element_count
        , ReachabilityRejectionReason initial_reason
    )
        : size{ element_count }
    {
        if (!uses_inline_storage()) {
            heap_words.assign(word_count(), 0u);
        }
        for (std::size_t i = 0; i < size; ++i) {
            set(i, initial_reason);
        }
    }

    std::size_t CompactReachabilityReasons::word_count() const noexcept {
        return (size + reasons_per_word - 1u) / reasons_per_word;
    }

    bool CompactReachabilityReasons::uses_inline_storage() const noexcept {
        return word_count() <= inline_word_count;
    }

    std::uint64_t CompactReachabilityReasons::word(
        std::size_t index
    ) const noexcept {
        return uses_inline_storage()
            ? inline_words[index]
            : heap_words[index];
    }

    std::uint64_t& CompactReachabilityReasons::word(
        std::size_t index
    ) noexcept {
        return uses_inline_storage()
            ? inline_words[index]
            : heap_words[index];
    }

    void CompactReachabilityReasons::set(
          std::size_t                 index
        , ReachabilityRejectionReason reason
    ) noexcept {
        if (index >= size) {
            return;
        }
        const auto word_index = index / reasons_per_word;
        const auto offset = (index % reasons_per_word) * bits_per_reason;
        auto& storage = word(word_index);
        storage &= ~(std::uint64_t{ 0b11 } << offset);
        storage |= static_cast<std::uint64_t>(reason) << offset;
    }

    ReachabilityRejectionReason CompactReachabilityReasons::get(
        std::size_t index
    ) const noexcept {
        if (index >= size) {
            return ReachabilityRejectionReason::UnreachableDestination;
        }
        const auto word_index = index / reasons_per_word;
        const auto offset = (index % reasons_per_word) * bits_per_reason;
        return static_cast<ReachabilityRejectionReason>(
            (word(word_index) >> offset) & std::uint64_t{ 0b11 }
        );
    }

    const ReachabilityMaskEntry& ReachabilityMaskCache::target_entry(
        const ResidualReachabilityKey& key
    ) {
        const auto existing = target_masks.find(key);
        if (existing != target_masks.end()) {
            return existing->second;
        }
        const auto [it, inserted] = target_masks.emplace(
              key
            , build_target_reachability_mask(
                  key
                , targets
                , reachability
                , max_transfers
              )
        );
        (void)inserted;
        return it->second;
    }

    const ReachabilityMaskEntry& ReachabilityMaskCache::slot_entry(
        const ResidualReachabilityKey& key
    ) {
        if (unified_completion_targets) {
            return target_entry(key);
        }
        const auto existing = slot_masks.find(key);
        if (existing != slot_masks.end()) {
            return existing->second;
        }
        const auto [it, inserted] = slot_masks.emplace(
              key
            , build_slot_reachability_mask(
                  key
                , slots
                , reachability
                , max_transfers
              )
        );
        (void)inserted;
        return it->second;
    }

    ResidualReachabilityKey reachability_mask_key(
          const SearchBranch&   branch
        , const TransferLimits& limits
    ) noexcept {
        return ResidualReachabilityKey{
              .current_physical    = branch.trace.current_physical
            , .phase               = branch.trace.phase
            , .remaining_transfers = remaining_transfer_budget(branch, limits)
        };
    }

    ActiveIndexSet filter_target_positions_by_reachability(
          const ActiveIndexSet&        target_positions
        , const ReachabilityMaskEntry& reachability_entry
    ) {
        return target_positions.intersect(reachability_entry.reachable);
    }

    ReachabilityRejectionReason summarize_target_reachability_rejection(
          const ActiveIndexSet&        target_positions
        , const ReachabilityMaskEntry& reachability_entry
    ) noexcept {
        const auto priority = [](ReachabilityRejectionReason reason) noexcept {
            switch (reason) {
                case ReachabilityRejectionReason::UnreachableDestination:
                    return 3;
                case ReachabilityRejectionReason::TransferBudget:
                    return 2;
                case ReachabilityRejectionReason::Phase:
                    return 1;
            }
            return 0;
        };
        auto result = ReachabilityRejectionReason::Phase;
        target_positions.for_each_difference_index(
              reachability_entry.reachable
            , [&](std::size_t target_pos) {
                  const auto reason =
                      reachability_entry.rejection_reasons.get(target_pos);
                  if (priority(reason) > priority(result)) {
                      result = reason;
                  }
              }
        );
        return result;
    }

    ReachabilityTaskFilter filter_task_positions_by_reachability(
          const ActiveIndexSet&        task_positions
        , const ReachabilityMaskEntry& reachability_entry
    ) {
        ReachabilityTaskFilter result{
              .reachable = task_positions.intersect(reachability_entry.reachable)
        };
        const auto active_count = task_positions.active_count();
        const auto reachable_count = result.reachable.active_count();
        result.unreachable.reserve(active_count - reachable_count);
        task_positions.for_each_difference_index(
              reachability_entry.reachable
            , [&](std::size_t task_pos) {
                  result.unreachable.push_back(
                      RejectedReachabilityTask{
                            .task_position = task_pos
                          , .reason =
                                reachability_entry.rejection_reasons.get(task_pos)
                      }
                  );
              }
        );
        return result;
    }

    void record_reachability_rejections(
          std::span<const RejectedReachabilityTask> rejected_tasks
        , std::vector<TaskSearchStats>&             task_stats
        , TaskSearchStats&                          stats
    ) noexcept {
        for (const auto rejected : rejected_tasks) {
            add_reachability_rejection(stats, rejected.reason);
            add_reachability_rejection(
                  task_stats[rejected.task_position]
                , rejected.reason
            );
        }
    }

    void record_suffix_lower_bound_rejections(
          std::span<const RejectedSuffixLowerBoundTask> rejected_tasks
        , std::vector<TaskSearchStats>&                 task_stats
        , TaskSearchStats&                              stats
    ) noexcept {
        for (const auto rejected : rejected_tasks) {
            add_suffix_lower_bound_rejection(stats, rejected.reason);
            add_suffix_lower_bound_rejection(
                  task_stats[rejected.task_position]
                , rejected.reason
            );
        }
    }

}  // namespace timetable::domain::assignment::runtime
