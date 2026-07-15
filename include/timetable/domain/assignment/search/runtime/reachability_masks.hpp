#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/pruning/suffix_lower_bound.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/params.hpp"

namespace timetable::domain::assignment::runtime {

    struct RejectedReachabilityTask final {
        std::size_t                 task_position{};
        ReachabilityRejectionReason reason{
            ReachabilityRejectionReason::UnreachableDestination
        };
    };

    struct RejectedSuffixLowerBoundTask final {
        std::size_t                     task_position{};
        SuffixLowerBoundRejectionReason reason{
            SuffixLowerBoundRejectionReason::ToleranceImpedance
        };
    };

    struct ReachabilityTaskFilter final {
        ActiveIndexSet reachable{};
        std::vector<RejectedReachabilityTask> unreachable{};
    };

    struct CompactReachabilityReasons final {
        static constexpr std::size_t inline_capacity = FixedActiveMask::max_size;
        static constexpr std::size_t bits_per_reason = 2u;
        static constexpr std::size_t reasons_per_word = 64u / bits_per_reason;
        static constexpr std::size_t inline_word_count =
            (inline_capacity + reasons_per_word - 1u) / reasons_per_word;

        std::size_t size{};
        std::array<std::uint64_t, inline_word_count> inline_words{};
        std::vector<std::uint64_t> heap_words{};

        CompactReachabilityReasons() = default;

        explicit CompactReachabilityReasons(
              std::size_t                 element_count
            , ReachabilityRejectionReason initial_reason
        );

        [[nodiscard]] std::size_t word_count() const noexcept;
        [[nodiscard]] bool uses_inline_storage() const noexcept;
        [[nodiscard]] std::uint64_t word(std::size_t index) const noexcept;
        [[nodiscard]] std::uint64_t& word(std::size_t index) noexcept;

        void set(
              std::size_t                 index
            , ReachabilityRejectionReason reason
        ) noexcept;

        [[nodiscard]] ReachabilityRejectionReason get(
            std::size_t index
        ) const noexcept;
    };

    struct ReachabilityMaskEntry final {
        ActiveIndexSet             reachable{};
        CompactReachabilityReasons rejection_reasons{};
    };

    struct ReachabilityMaskCache final {
        const ResidualReachability&             reachability;
        std::span<const SearchCompletionTarget> targets;
        std::span<const SearchProjectionSlot>   slots;
        TransferCount                           max_transfers;
        bool                                    unified_completion_targets{};
        std::unordered_map<
              ResidualReachabilityKey
            , ReachabilityMaskEntry
            , ResidualReachabilityKeyHash
        > target_masks{};
        std::unordered_map<
              ResidualReachabilityKey
            , ReachabilityMaskEntry
            , ResidualReachabilityKeyHash
        > slot_masks{};

        [[nodiscard]] const ReachabilityMaskEntry& target_entry(
            const ResidualReachabilityKey& key
        );

        [[nodiscard]] const ReachabilityMaskEntry& slot_entry(
            const ResidualReachabilityKey& key
        );
    };

    [[nodiscard]] ResidualReachabilityKey reachability_mask_key(
          const SearchBranch&   branch
        , const TransferLimits& limits
    ) noexcept;

    [[nodiscard]] ActiveIndexSet filter_target_positions_by_reachability(
          const ActiveIndexSet&        target_positions
        , const ReachabilityMaskEntry& reachability_entry
    );

    [[nodiscard]] ReachabilityRejectionReason summarize_target_reachability_rejection(
          const ActiveIndexSet&        target_positions
        , const ReachabilityMaskEntry& reachability_entry
    ) noexcept;

    [[nodiscard]] ReachabilityTaskFilter filter_task_positions_by_reachability(
          const ActiveIndexSet&        task_positions
        , const ReachabilityMaskEntry& reachability_entry
    );

    void record_reachability_rejections(
          std::span<const RejectedReachabilityTask> rejected_tasks
        , std::vector<TaskSearchStats>&             task_stats
        , TaskSearchStats&                          stats
    ) noexcept;

    void record_suffix_lower_bound_rejections(
          std::span<const RejectedSuffixLowerBoundTask> rejected_tasks
        , std::vector<TaskSearchStats>&                 task_stats
        , TaskSearchStats&                              stats
    ) noexcept;

}  // namespace timetable::domain::assignment::runtime
