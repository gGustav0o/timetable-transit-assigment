#include "timetable/domain/assignment/search/search.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <boost/unordered/unordered_flat_map.hpp>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_time_domain_diagnostics.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/assignment/search/branch_state.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/preprocessing/segments_index.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        using PartialPruningMetrics = SearchPruningMetrics;
        using NodeMetricSet         = SearchPruningMetricSet;
        using SearchNodeKey         = SearchPruningStateKey;


        // TODO??
        struct SearchNodeKeyHash final {
            std::size_t operator()(const SearchNodeKey& key) const noexcept {
                std::size_t seed = 17u;
                seed = seed * 31u + std::hash<std::int64_t>{}(static_cast<std::int64_t>(key.physical.kind));
                seed = seed * 31u + std::hash<std::int64_t>{}(key.physical.id);
                seed = seed * 31u + std::hash<bool>{}(key.occurrence.has_value());
                if (key.occurrence.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->stop    .get());
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.occurrence->position.get());
                }
                seed = seed * 31u + std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.phase));
                seed = seed * 31u + std::hash<bool>{}(key.transfer.last_trip.has_value());
                if (key.transfer.last_trip.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.transfer.last_trip->get());
                }
                seed = seed * 31u + std::hash<bool>{}(key.transfer.last_line.has_value());
                if (key.transfer.last_line.has_value()) {
                    seed = seed * 31u + std::hash<std::int64_t>{}(key.transfer.last_line->get());
                }
                return seed;
            }
        };

        /**
         * @brief Structural prefix of a connection explored by search.
         *
         * This is the trace state: current location, predecessor link and the
         * timed context needed to decide future feasible extensions.
         */
        struct SearchPartialTrace final {
            ZoneId                             origin{};
            EndpointKey                        current_physical{};
            std::optional<StopOccurrenceKey>   current_occurrence{};
            SearchBranchPhase                  phase{ SearchBranchPhase::AtOrigin };
            std::optional<std::size_t>         parent_branch{};
            std::optional<ConnectionSegmentId> incoming_segment{};
            const ConnectionSegment*           last_timed_segment{};
            const RouteSegment*                last_timed_route_segment{};
        };

        /**
         * @brief Incremental metrics of a partial connection prefix.
         *
         * The time, transfer and fare fields are parameter-independent base
         * metrics. capacity_exposure is a separate projection over a fixed
         * exogenous load snapshot for capacity-aware search; it is not a
         * post-assignment overload assessment.
         */
        struct SearchPartialMetrics final {
            std::optional<Time> departure{};
            std::optional<Time> current_time{};
            Time                access_time{};
            Time                in_vehicle_time{};
            Time                transfer_wait_time{};
            Time                transfer_walk_time{};
            Time                egress_time{};
            TransferCount       transfers{};
            double              fare{};
            CapacityExposure    capacity_exposure{};
        };

        struct SearchBranch final {
            SearchPartialTrace   trace{};
            SearchPartialMetrics metrics{};
        };

        struct BranchSlot final {
            std::unique_ptr<SearchBranch> branch{};
            std::optional<std::size_t>    parent{};
            std::size_t                   live_children{};
            bool                          self_released{};
        };

        using BranchArena = std::deque<BranchSlot>;

        [[nodiscard]] const SearchBranch& branch_at(
              const BranchArena& branches
            , std::size_t        index
        ) {
            return *branches.at(index).branch;
        }

        [[nodiscard]] SearchBranch& branch_at(
              BranchArena& branches
            , std::size_t  index
        ) {
            return *branches.at(index).branch;
        }

        std::size_t append_branch(
              BranchArena& branches
            , SearchBranch branch
        ) {
            const auto parent = branch.trace.parent_branch;
            if (parent.has_value()) {
                ++branches.at(*parent).live_children;
            }
            branches.push_back(
                BranchSlot{
                      .branch = std::make_unique<SearchBranch>(std::move(branch))
                    , .parent = parent
                }
            );
            return branches.size() - 1u;
        }

        template <typename ReleasePayload>
        void release_branch_if_closed(
              BranchArena&     branches
            , std::size_t      index
            , ReleasePayload&& release_payload
        ) {
            auto&& release = release_payload;
            auto cursor = std::optional<std::size_t>{ index };
            while (cursor.has_value()) {
                auto& slot = branches.at(*cursor);
                slot.self_released = true;
                if (slot.live_children != 0u || slot.branch == nullptr) {
                    return;
                }

                const auto parent = slot.parent;
                slot.branch.reset();
                release(*cursor);

                if (!parent.has_value()) {
                    return;
                }

                auto& parent_slot = branches.at(*parent);
                if (parent_slot.live_children == 0u) {
                    return;
                }
                --parent_slot.live_children;
                if (!parent_slot.self_released) {
                    return;
                }
                cursor = parent;
            }
        }

        enum class ReachabilityRejectionReason : std::uint8_t {
              Phase
            , TransferBudget
            , UnreachableDestination
        };

        struct ReachabilityRejectionStats final {
            std::size_t phase{};
            std::size_t transfer_budget{};
            std::size_t unreachable_destination{};
        };

        struct WalkKindStats final {
            std::size_t access{};
            std::size_t transfer{};
            std::size_t egress{};
        };

        struct BranchPhaseStats final {
            std::size_t at_origin{};
            std::size_t before_first_boarding{};
            std::size_t after_timed_ride{};
            std::size_t after_transfer_walk{};
            std::size_t completed{};
        };

        void increment_walk_kind_stats(
              WalkKindStats&    stats
            , ConnectionLegKind kind
        ) noexcept {
            switch (kind) {
                case ConnectionLegKind::AccessWalk:
                    ++stats.access;
                    return;

                case ConnectionLegKind::TransferWalk:
                    ++stats.transfer;
                    return;

                case ConnectionLegKind::EgressWalk:
                    ++stats.egress;
                    return;

                case ConnectionLegKind::Ride:
                case ConnectionLegKind::InitialWait:
                case ConnectionLegKind::TransferWait:
                case ConnectionLegKind::FinalWait:
                    return;
            }
        }

        [[nodiscard]] std::size_t& phase_counter(
              BranchPhaseStats& stats
            , SearchBranchPhase phase
        ) noexcept {
            switch (phase) {
                case SearchBranchPhase::AtOrigin:
                    return stats.at_origin;

                case SearchBranchPhase::BeforeFirstBoarding:
                    return stats.before_first_boarding;

                case SearchBranchPhase::AfterTimedRide:
                    return stats.after_timed_ride;

                case SearchBranchPhase::AfterTransferWalk:
                    return stats.after_transfer_walk;

                case SearchBranchPhase::Completed:
                    return stats.completed;
            }

            return stats.completed;
        }

        void increment_phase_stats(
              BranchPhaseStats& stats
            , SearchBranchPhase phase
        ) noexcept {
            ++phase_counter(stats, phase);
        }

        void decrement_phase_stats(
              BranchPhaseStats& stats
            , SearchBranchPhase phase
        ) noexcept {
            auto& counter = phase_counter(stats, phase);
            if (counter > 0u) {
                --counter;
            }
        }

        [[nodiscard]] std::string format_walk_kind_stats(
            const WalkKindStats& stats
        ) {
            return fmt::format(
                  "access/transfer/egress={}/{}/{}"
                , stats.access
                , stats.transfer
                , stats.egress
            );
        }

        [[nodiscard]] std::string format_branch_phase_stats(
            const BranchPhaseStats& stats
        ) {
            return fmt::format(
                  "origin/preboard/timed/transfer_walk/completed={}/{}/{}/{}/{}"
                , stats.at_origin
                , stats.before_first_boarding
                , stats.after_timed_ride
                , stats.after_transfer_walk
                , stats.completed
            );
        }

        enum class SuffixLowerBoundRejectionReason : std::uint8_t {
              ExactDominance
            , ToleranceImpedance
            , ToleranceJourneyTime
            , ToleranceTransfers
        };

        struct SuffixLowerBoundRejectionStats final {
            std::size_t exact_dominance{};
            std::size_t tolerance_impedance{};
            std::size_t tolerance_journey_time{};
            std::size_t tolerance_transfers{};
        };

        struct TaskSearchStats final {
            std::size_t expanded_branches{};
            std::size_t generated_successors{};
            std::size_t accepted_branches{};
            WalkKindStats generated_walk{};
            WalkKindStats accepted_walk{};
            std::size_t rejected_consecutive_walk{};
            BranchPhaseStats accepted_branches_by_phase{};
            std::size_t rejected_time_domain{};
            std::size_t rejected_feasibility{};
            std::size_t rejected_reboarding{};
            std::size_t rejected_cycles{};
            std::size_t rejected_transfer_limit{};
            std::size_t rejected_reachability{};
            ReachabilityRejectionStats reachability_rejections{};
            std::size_t rejected_suffix_lower_bound{};
            SuffixLowerBoundRejectionStats suffix_lower_bound_rejections{};
            std::size_t rejected_dominance_or_tolerance{};
            std::size_t completed_connections{};
            std::size_t rejected_complete_admissibility{};
            std::size_t rejected_complete_dominance{};
            std::size_t removed_complete_dominated{};
            std::size_t rejected_complete_tolerance{};
            std::size_t max_current_frontier{};
            std::size_t max_next_frontier{};
            SearchPruningRuntimeStats pruning{};
        };

        [[nodiscard]] mathfp::Expected<std::map<IntervalId, const TimeInterval*>> interval_lookup(
            const InputModel& input
        ) {
            std::map<IntervalId, const TimeInterval*> lookup;
            for (const auto& interval : input.intervals) {
                if (!lookup.emplace(interval.id, &interval).second) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate interval id while building search tasks")
                            .ctx("interval_id", interval.id.get())
                    );
                }
            }
            return lookup;
        }

        using NodeMetricMap = boost::unordered_flat_map<
              SearchNodeKey
            , NodeMetricSet
            , SearchNodeKeyHash
        >;

        enum class SearchProjectionSlotKind : std::uint8_t {
              DemandTask
            , CompletionTarget
        };

        struct SearchProjectionSlot final {
            SearchProjectionSlotKind                 kind{ SearchProjectionSlotKind::DemandTask };
            ZoneId                                   origin{};
            ZoneId                                   destination{};
            std::optional<IntervalId>                interval{};
            std::optional<SearchTaskRef>             task_ref{};
            const SearchTask*                        task{};
            std::optional<std::size_t>               result_index{};
            std::optional<SearchCompletionTargetRef> completion_target{};
        };

        struct CompactCompleteConnectionRetention final {
            std::vector<std::vector<ConnectionSegmentId>> traces{};
            std::vector<CompleteConnectionMetrics>        metrics{};
        };

        /**
         * @brief Retained complete-connection state for one projection slot.
         *
         * Complete alternatives are projection-local: DemandTasks retain
         * OD-interval alternatives, while CompletionTargets retain alternatives
         * for one all-zone target destination. Partial-prefix pruning may be
         * either projection-local or tree-global depending on
         * SearchPartialRetentionScope.
         */
        struct SearchProjectionRetention final {
            SearchProjectionSlot slot{};
            NodeMetricMap known_metrics{};
            CompleteConnectionRetention complete_connections{};
            CompactCompleteConnectionRetention compact_complete_connections{};
        };

        struct TreePartialRetention final {
            NodeMetricMap known_metrics{};
        };

        struct SearchSlotResult final {
            SearchProjectionSlot          slot{};
            std::size_t                   connection_count{};
            std::vector<SearchConnection> connections{};
        };

        struct SearchBatchKey final {
            ZoneId                        origin{};
            std::optional<IntervalId>      interval{};
            std::vector<SearchTimeWindow> departure_windows{};
        };

        [[nodiscard]] bool operator<(
              const SearchBatchKey& lhs
            , const SearchBatchKey& rhs
        ) noexcept {
            if (lhs.origin != rhs.origin) {
                return lhs.origin < rhs.origin;
            }
            if (lhs.interval.has_value() != rhs.interval.has_value()) {
                return !lhs.interval.has_value() && rhs.interval.has_value();
            }
            if (lhs.interval.has_value() && *lhs.interval != *rhs.interval) {
                return *lhs.interval < *rhs.interval;
            }
            const auto common_size = std::min(
                  lhs.departure_windows.size()
                , rhs.departure_windows.size()
            );
            for (std::size_t i = 0; i < common_size; ++i) {
                if (lhs.departure_windows[i].begin.value() != rhs.departure_windows[i].begin.value()) {
                    return lhs.departure_windows[i].begin.value() < rhs.departure_windows[i].begin.value();
                }
                if (lhs.departure_windows[i].end.value() != rhs.departure_windows[i].end.value()) {
                    return lhs.departure_windows[i].end.value() < rhs.departure_windows[i].end.value();
                }
            }
            return lhs.departure_windows.size() < rhs.departure_windows.size();
        }

        struct SearchBatch final {
            SearchBatchKey                         key{};
            const SearchTimeDomain*                departure_domain{};
            std::vector<SearchCompletionTarget>    completion_targets{};
            std::vector<SearchProjectionSlot>      projection_slots{};
        };

        [[nodiscard]] std::string format_batch_interval(
            const std::optional<IntervalId>& interval
        ) {
            if (!interval.has_value()) {
                return "period";
            }
            return std::to_string(interval->get());
        }

        struct ResidualReachabilityKey final {
            EndpointKey       current_physical{};
            SearchBranchPhase phase{ SearchBranchPhase::AtOrigin };
            TransferCount     remaining_transfers{};
        };

        [[nodiscard]] bool operator==(
              const ResidualReachabilityKey& lhs
            , const ResidualReachabilityKey& rhs
        ) noexcept {
            return lhs.current_physical    == rhs.current_physical
                && lhs.phase               == rhs.phase
                && lhs.remaining_transfers == rhs.remaining_transfers;
        }

        struct ResidualReachabilityKeyHash final {
            std::size_t operator()(const ResidualReachabilityKey& key) const noexcept {
                std::size_t seed = 17u;
                seed = seed * 31u + std::hash<EndpointKey>{}(key.current_physical);
                seed = seed * 31u + std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.phase));
                seed = seed * 31u + std::hash<std::int32_t>{}(key.remaining_transfers.get());
                return seed;
            }
        };

        struct ResidualReverseEdge final {
            EndpointKey predecessor{};
            Time        run_time{};
        };

        struct ResidualReverseGraph final {
            std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> walk_predecessors_by_node{};
            std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>> timed_predecessors_by_node{};
        };

        struct ResidualPhysicalEdgeKey final {
            EndpointKey from{};
            EndpointKey to{};
        };

        [[nodiscard]] bool operator==(
              const ResidualPhysicalEdgeKey& lhs
            , const ResidualPhysicalEdgeKey& rhs
        ) noexcept {
            return lhs.from == rhs.from && lhs.to == rhs.to;
        }

        struct ResidualPhysicalEdgeKeyHash final {
            std::size_t operator()(const ResidualPhysicalEdgeKey& key) const noexcept {
                std::size_t seed = 17u;
                seed = seed * 31u + std::hash<EndpointKey>{}(key.from);
                seed = seed * 31u + std::hash<EndpointKey>{}(key.to);
                return seed;
            }
        };

        struct ResidualSuffixLowerBounds final {
            Time          journey_time{};
            TransferCount transfers{};
            double        impedance{};
        };

        struct DestinationResidualReachability final {
            std::unordered_set<ResidualReachabilityKey, ResidualReachabilityKeyHash> reachable_states{};
            std::unordered_map<
                  ResidualReachabilityKey
                , ResidualSuffixLowerBounds
                , ResidualReachabilityKeyHash
            > suffix_lower_bounds{};
        };

        struct ResidualReachability final {
            std::map<ZoneId, DestinationResidualReachability> destinations{};
        };

        struct ReachabilityDecision final {
            bool                        feasible{};
            ReachabilityRejectionReason rejection_reason{ ReachabilityRejectionReason::UnreachableDestination };
        };

        struct RejectedReachabilityTask final {
            std::size_t                 task_position{};
            ReachabilityRejectionReason reason{ ReachabilityRejectionReason::UnreachableDestination };
        };

        struct RejectedSuffixLowerBoundTask final {
            std::size_t                     task_position{};
            SuffixLowerBoundRejectionReason reason{ SuffixLowerBoundRejectionReason::ToleranceImpedance };
        };

        struct ActiveIndexSet final {
            static constexpr std::size_t word_bits = 64;
            static constexpr std::size_t inline_word_count = 4;

            std::size_t size{};
            std::array<std::uint64_t, inline_word_count> inline_words{};
            std::vector<std::uint64_t> heap_words{};

            ActiveIndexSet() = default;

            explicit ActiveIndexSet(std::size_t element_count)
                : size{ element_count }
            {
                if (!uses_inline_storage()) {
                    heap_words.assign(word_count(), 0u);
                }
            }

            [[nodiscard]] static ActiveIndexSet full(std::size_t element_count) {
                ActiveIndexSet result{ element_count };
                for (std::size_t i = 0; i < result.word_count(); ++i) {
                    result.word(i) = ~std::uint64_t{ 0 };
                }
                const auto tail_bits = element_count % word_bits;
                if (result.word_count() != 0u && tail_bits != 0u) {
                    result.word(result.word_count() - 1u) &=
                        (std::uint64_t{ 1 } << tail_bits) - 1u;
                }
                return result;
            }

            [[nodiscard]] std::size_t word_count() const noexcept {
                return (size + word_bits - 1u) / word_bits;
            }

            [[nodiscard]] bool uses_inline_storage() const noexcept {
                return word_count() <= inline_word_count;
            }

            [[nodiscard]] std::uint64_t word(std::size_t index) const noexcept {
                return uses_inline_storage()
                    ? inline_words[index]
                    : heap_words[index];
            }

            [[nodiscard]] std::uint64_t& word(std::size_t index) noexcept {
                return uses_inline_storage()
                    ? inline_words[index]
                    : heap_words[index];
            }

            [[nodiscard]] bool empty() const noexcept {
                for (std::size_t i = 0; i < word_count(); ++i) {
                    if (word(i) != 0u) {
                        return false;
                    }
                }
                return true;
            }

            [[nodiscard]] std::size_t active_count() const noexcept {
                std::size_t count = 0;
                for (std::size_t i = 0; i < word_count(); ++i) {
                    count += static_cast<std::size_t>(std::popcount(word(i)));
                }
                return count;
            }

            [[nodiscard]] bool contains(std::size_t index) const noexcept {
                return index < size
                    && (word(index / word_bits)
                        & (std::uint64_t{ 1 } << (index % word_bits))) != 0u;
            }

            void set(std::size_t index) noexcept {
                if (index >= size) {
                    return;
                }
                word(index / word_bits) |= std::uint64_t{ 1 } << (index % word_bits);
            }

            [[nodiscard]] ActiveIndexSet intersect(
                const ActiveIndexSet& rhs
            ) const {
                ActiveIndexSet result{ std::min(size, rhs.size) };
                const auto common_words = std::min(word_count(), rhs.word_count());
                for (std::size_t i = 0; i < common_words; ++i) {
                    result.word(i) = word(i) & rhs.word(i);
                }
                return result;
            }

            [[nodiscard]] bool equals(const ActiveIndexSet& rhs) const noexcept {
                if (size != rhs.size) {
                    return false;
                }
                for (std::size_t i = 0; i < word_count(); ++i) {
                    if (word(i) != rhs.word(i)) {
                        return false;
                    }
                }
                return true;
            }

            template <typename Visitor>
            void for_each_index(Visitor&& visit) const {
                auto&& visitor = visit;
                for (std::size_t word_index = 0; word_index < word_count(); ++word_index) {
                    auto bits = word(word_index);
                    while (bits != 0u) {
                        const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
                        const auto index = word_index * word_bits + bit;
                        if (index < size) {
                            visitor(index);
                        }
                        bits &= bits - 1u;
                    }
                }
            }

            template <typename Visitor>
            void for_each_difference_index(
                  const ActiveIndexSet& rhs
                , Visitor&&             visit
            ) const {
                auto&& visitor = visit;
                const auto lhs_words = word_count();
                const auto rhs_words = rhs.word_count();
                for (std::size_t word_index = 0; word_index < lhs_words; ++word_index) {
                    const auto rhs_word = word_index < rhs_words
                        ? rhs.word(word_index)
                        : std::uint64_t{ 0 };
                    auto bits = word(word_index) & ~rhs_word;
                    while (bits != 0u) {
                        const auto bit = static_cast<std::size_t>(std::countr_zero(bits));
                        const auto index = word_index * word_bits + bit;
                        if (index < size) {
                            visitor(index);
                        }
                        bits &= bits - 1u;
                    }
                }
            }
        };

        struct FixedActiveMask final {
            static constexpr std::size_t max_words = ActiveIndexSet::inline_word_count;
            static constexpr std::size_t max_size  = max_words * ActiveIndexSet::word_bits;

            std::size_t size{};
            std::array<std::uint64_t, max_words> words{};

            [[nodiscard]] static FixedActiveMask from(
                const ActiveIndexSet& source
            ) noexcept {
                FixedActiveMask result{
                      .size = source.size
                };
                const auto copied_words = std::min(source.word_count(), max_words);
                for (std::size_t i = 0; i < copied_words; ++i) {
                    result.words[i] = source.word(i);
                }
                return result;
            }

            [[nodiscard]] ActiveIndexSet to_active_index_set() const {
                ActiveIndexSet result{ size };
                const auto copied_words = std::min(result.word_count(), max_words);
                for (std::size_t i = 0; i < copied_words; ++i) {
                    result.word(i) = words[i];
                }
                return result;
            }
        };

        struct DemandBranchProjectionState final {
            ActiveIndexSet active_tasks{};
            ActiveIndexSet active_targets{};
        };

        constexpr std::size_t kTaskProgressStep    = 10;
        constexpr std::size_t kSearchHeartbeatStep = 100'000;
        constexpr std::size_t kSearchWallClockSuccessorCheckStep = 16'384;
        constexpr std::size_t kInitialTaskBranchReserve = 4'096;
        constexpr std::size_t kAllZoneMaxParallelBatches = 4;
        constexpr auto kSearchWallClockHeartbeatInterval =
            std::chrono::seconds{ 30 };

        const RouteSegment& route_segment_at(
              const PreprocessedNetwork& network
            , RouteSegmentId             id
        ) {
            return network.route_segments.at(static_cast<std::size_t>(id.get()));
        }

        const ConnectionSegment& connection_segment_at(
              const PreprocessedNetwork& network
            , ConnectionSegmentId        id
        ) {
            return network.connection_segments.at(static_cast<std::size_t>(id.get()));
        }

        using ResidualPhysicalEdgeTimes = std::unordered_map<
              ResidualPhysicalEdgeKey
            , Time
            , ResidualPhysicalEdgeKeyHash
        >;

        void retain_min_residual_edge_time(
              ResidualPhysicalEdgeTimes& edge_times
            , ResidualPhysicalEdgeKey    key
            , Time                       run_time
        ) {
            const auto [it, inserted] = edge_times.emplace(key, run_time);
            if (!inserted && run_time.value() < it->second.value()) {
                it->second = run_time;
            }
        }

        void append_residual_reverse_edges(
              std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>>& target
            , const ResidualPhysicalEdgeTimes&                                   edge_times
        ) {
            for (const auto& [key, run_time] : edge_times) {
                target[key.to].push_back(
                    ResidualReverseEdge{
                          .predecessor = key.from
                        , .run_time    = run_time
                    }
                );
                target.try_emplace(key.from);
            }
        }

        [[nodiscard]] ResidualReachabilityKey residual_reachability_key(
            const RelaxedSuffixState& state
        ) noexcept {
            return ResidualReachabilityKey{
                  .current_physical    = state.current_physical
                , .phase               = state.phase
                , .remaining_transfers = state.remaining_transfers
            };
        }

        [[nodiscard]] ResidualReverseGraph build_residual_reverse_graph(
              std::span<const RouteSegment>      route_segments
            , std::span<const ConnectionSegment> connection_segments
        ) {
            ResidualPhysicalEdgeTimes walk_edges;
            ResidualPhysicalEdgeTimes timed_edges;

            for (const auto& segment : route_segments) {
                if (is_walk(segment)) {
                    retain_min_residual_edge_time(
                          walk_edges
                        , ResidualPhysicalEdgeKey{
                              .from = physical_from_key(segment)
                            , .to   = physical_to_key(segment)
                          }
                        , segment.run_time
                    );
                }
            }

            for (const auto& segment : connection_segments) {
                if (!is_timed_connection(segment)
                    || !segment.departure.has_value()
                    || !segment.arrival.has_value()) {
                    continue;
                }
                const auto& route_segment = route_segments[static_cast<std::size_t>(
                    segment.route_segment.get()
                )];
                retain_min_residual_edge_time(
                      timed_edges
                    , ResidualPhysicalEdgeKey{
                          .from = physical_from_key(route_segment)
                        , .to   = physical_to_key(route_segment)
                      }
                    , Time{ segment.arrival->value() - segment.departure->value() }
                );
            }

            ResidualReverseGraph graph;
            append_residual_reverse_edges(graph.walk_predecessors_by_node, walk_edges);
            append_residual_reverse_edges(graph.timed_predecessors_by_node, timed_edges);
            return graph;
        }

        [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
              const SearchBranch&   branch
            , const SearchTask&     task
            , const TransferLimits& limits
        ) noexcept;

        [[nodiscard]] std::size_t reachability_rejection_count(
            const ReachabilityRejectionStats& stats
        ) noexcept {
            return stats.phase
                 + stats.transfer_budget
                 + stats.unreachable_destination;
        }

        [[nodiscard]] std::size_t suffix_lower_bound_rejection_count(
            const SuffixLowerBoundRejectionStats& stats
        ) noexcept {
            return stats.exact_dominance
                 + stats.tolerance_impedance
                 + stats.tolerance_journey_time
                 + stats.tolerance_transfers;
        }

        mathfp::Expected<mathfp::Unit> validate_reachability_rejection_stats(
            const TaskSearchStats& stats
        ) {
            const auto detail_count = reachability_rejection_count(stats.reachability_rejections);
            if (detail_count != stats.rejected_reachability) {
                return mathfp::unexpected(
                    mathfp::internal_error("reachability rejection diagnostics do not sum to total")
                        .ctx("total", static_cast<std::int64_t>(stats.rejected_reachability))
                        .ctx("detail", static_cast<std::int64_t>(detail_count))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_suffix_lower_bound_rejection_stats(
            const TaskSearchStats& stats
        ) {
            const auto detail_count = suffix_lower_bound_rejection_count(
                stats.suffix_lower_bound_rejections
            );
            if (detail_count != stats.rejected_suffix_lower_bound) {
                return mathfp::unexpected(
                    mathfp::internal_error("suffix lower-bound rejection diagnostics do not sum to total")
                        .ctx("total", static_cast<std::int64_t>(stats.rejected_suffix_lower_bound))
                        .ctx("detail", static_cast<std::int64_t>(detail_count))
                );
            }
            return mathfp::kUnit;
        }

        void add_reachability_rejection(
              TaskSearchStats&            stats
            , ReachabilityRejectionReason reason
        ) noexcept {
            ++stats.rejected_reachability;
            switch (reason) {
                case ReachabilityRejectionReason::Phase:
                    ++stats.reachability_rejections.phase;
                    return;
                case ReachabilityRejectionReason::TransferBudget:
                    ++stats.reachability_rejections.transfer_budget;
                    return;
                case ReachabilityRejectionReason::UnreachableDestination:
                    ++stats.reachability_rejections.unreachable_destination;
                    return;
            }
        }

        void add_suffix_lower_bound_rejection(
              TaskSearchStats&                 stats
            , SuffixLowerBoundRejectionReason reason
        ) noexcept {
            ++stats.rejected_suffix_lower_bound;
            switch (reason) {
                case SuffixLowerBoundRejectionReason::ExactDominance:
                    ++stats.suffix_lower_bound_rejections.exact_dominance;
                    return;
                case SuffixLowerBoundRejectionReason::ToleranceImpedance:
                    ++stats.suffix_lower_bound_rejections.tolerance_impedance;
                    return;
                case SuffixLowerBoundRejectionReason::ToleranceJourneyTime:
                    ++stats.suffix_lower_bound_rejections.tolerance_journey_time;
                    return;
                case SuffixLowerBoundRejectionReason::ToleranceTransfers:
                    ++stats.suffix_lower_bound_rejections.tolerance_transfers;
                    return;
            }
        }

        [[nodiscard]] bool has_reachable_state(
              const DestinationResidualReachability& destination
            , const ResidualReachabilityKey&         state
        ) noexcept {
            return destination.reachable_states.contains(state);
        }

        [[nodiscard]] bool has_more_budget_state(
              const DestinationResidualReachability& destination
            , const ResidualReachabilityKey&         state
            , TransferCount                          max_transfers
        ) noexcept {
            for (
                auto remaining = state.remaining_transfers.get() + 1;
                remaining <= max_transfers.get();
                ++remaining
            ) {
                if (has_reachable_state(
                      destination
                    , ResidualReachabilityKey{
                          .current_physical    = state.current_physical
                        , .phase               = state.phase
                        , .remaining_transfers = TransferCount{ remaining }
                      }
                )) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool has_any_phase_state_at_endpoint(
              const DestinationResidualReachability& destination
            , EndpointKey                            endpoint
        ) noexcept {
            return std::any_of(
                  destination.reachable_states.begin()
                , destination.reachable_states.end()
                , [&](const ResidualReachabilityKey& state) {
                    return state.current_physical == endpoint;
                }
            );
        }

        void enqueue_reachable_state(
              DestinationResidualReachability& destination
            , std::deque<ResidualReachabilityKey>& frontier
            , ResidualReachabilityKey              state
        ) {
            if (destination.reachable_states.insert(state).second) {
                frontier.push_back(state);
            }
        }

        void enqueue_walk_predecessors(
              const ResidualReverseGraph&          graph
            , DestinationResidualReachability&      destination
            , std::deque<ResidualReachabilityKey>& frontier
            , const ResidualReachabilityKey&        state
        ) {
            const auto predecessors = graph.walk_predecessors_by_node.find(state.current_physical);
            if (predecessors == graph.walk_predecessors_by_node.end()) {
                return;
            }

            if (state.phase == SearchBranchPhase::Completed) {
                // Reverse of AfterTimedRide --egress walk--> Completed.
                for (const auto edge : predecessors->second) {
                    const auto predecessor = edge.predecessor;
                    if (predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTimedRide
                            , .remaining_transfers = state.remaining_transfers
                          }
                    );
                }
                return;
            }

            if (state.phase == SearchBranchPhase::AfterTransferWalk) {
                // Reverse of AfterTimedRide --transfer walk--> AfterTransferWalk.
                for (const auto edge : predecessors->second) {
                    const auto predecessor = edge.predecessor;
                    if (predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTimedRide
                            , .remaining_transfers = state.remaining_transfers
                          }
                    );
                }
                return;
            }

            if (state.phase == SearchBranchPhase::BeforeFirstBoarding) {
                // Reverse of AtOrigin --access walk--> BeforeFirstBoarding.
                for (const auto edge : predecessors->second) {
                    const auto predecessor = edge.predecessor;
                    if (predecessor.kind != EndpointKind::Zone) {
                        continue;
                    }
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AtOrigin
                            , .remaining_transfers = state.remaining_transfers
                          }
                    );
                }
            }
        }

        void enqueue_timed_predecessors(
              const ResidualReverseGraph&          graph
            , DestinationResidualReachability&      destination
            , std::deque<ResidualReachabilityKey>& frontier
            , const ResidualReachabilityKey&        state
            , TransferCount                         max_transfers
        ) {
            if (state.phase != SearchBranchPhase::AfterTimedRide) {
                return;
            }

            const auto predecessors = graph.timed_predecessors_by_node.find(state.current_physical);
            if (predecessors == graph.timed_predecessors_by_node.end()) {
                return;
            }

            for (const auto edge : predecessors->second) {
                const auto predecessor = edge.predecessor;
                if (predecessor.kind != EndpointKind::Stop) {
                    continue;
                }

                // First timed boarding does not count as a transfer.
                enqueue_reachable_state(
                      destination
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = predecessor
                        , .phase               = SearchBranchPhase::BeforeFirstBoarding
                        , .remaining_transfers = state.remaining_transfers
                      }
                );

                // Every later timed boarding consumes one remaining transfer.
                if (state.remaining_transfers < max_transfers) {
                    // Reverse of AfterTimedRide --timed ride--> AfterTimedRide.
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTimedRide
                            , .remaining_transfers = TransferCount{
                                  state.remaining_transfers.get() + 1
                              }
                          }
                    );
                    // Reverse of AfterTransferWalk --timed ride--> AfterTimedRide.
                    enqueue_reachable_state(
                          destination
                        , frontier
                        , ResidualReachabilityKey{
                              .current_physical    = predecessor
                            , .phase               = SearchBranchPhase::AfterTransferWalk
                            , .remaining_transfers = TransferCount{
                                  state.remaining_transfers.get() + 1
                              }
                          }
                    );
                }
            }
        }

        enum class ResidualTransitionKind : std::uint8_t {
              AccessWalk
            , TransferWalk
            , EgressWalk
            , FirstTimedRide
            , TransferTimedRide
        };

        struct ResidualPredecessorTransition final {
            ResidualReachabilityKey predecessor{};
            Time                    run_time{};
            ResidualTransitionKind  kind{ ResidualTransitionKind::AccessWalk };
        };

        template <typename Visitor>
        void for_each_residual_predecessor_transition(
              const ResidualReverseGraph&   graph
            , const ResidualReachabilityKey& state
            , TransferCount                  max_transfers
            , Visitor&&                      visit
        ) {
            auto&& visitor = visit;

            const auto walk_predecessors = graph.walk_predecessors_by_node.find(state.current_physical);
            if (walk_predecessors != graph.walk_predecessors_by_node.end()) {
                if (state.phase == SearchBranchPhase::Completed) {
                    for (const auto edge : walk_predecessors->second) {
                        if (edge.predecessor.kind != EndpointKind::Stop) {
                            continue;
                        }
                        visitor(ResidualPredecessorTransition{
                              .predecessor = ResidualReachabilityKey{
                                    .current_physical    = edge.predecessor
                                  , .phase               = SearchBranchPhase::AfterTimedRide
                                  , .remaining_transfers = state.remaining_transfers
                                }
                            , .run_time    = edge.run_time
                            , .kind        = ResidualTransitionKind::EgressWalk
                        });
                    }
                } else if (state.phase == SearchBranchPhase::AfterTransferWalk) {
                    for (const auto edge : walk_predecessors->second) {
                        if (edge.predecessor.kind != EndpointKind::Stop) {
                            continue;
                        }
                        visitor(ResidualPredecessorTransition{
                              .predecessor = ResidualReachabilityKey{
                                    .current_physical    = edge.predecessor
                                  , .phase               = SearchBranchPhase::AfterTimedRide
                                  , .remaining_transfers = state.remaining_transfers
                                }
                            , .run_time    = edge.run_time
                            , .kind        = ResidualTransitionKind::TransferWalk
                        });
                    }
                } else if (state.phase == SearchBranchPhase::BeforeFirstBoarding) {
                    for (const auto edge : walk_predecessors->second) {
                        if (edge.predecessor.kind != EndpointKind::Zone) {
                            continue;
                        }
                        visitor(ResidualPredecessorTransition{
                              .predecessor = ResidualReachabilityKey{
                                    .current_physical    = edge.predecessor
                                  , .phase               = SearchBranchPhase::AtOrigin
                                  , .remaining_transfers = state.remaining_transfers
                                }
                            , .run_time    = edge.run_time
                            , .kind        = ResidualTransitionKind::AccessWalk
                        });
                    }
                }
            }

            if (state.phase != SearchBranchPhase::AfterTimedRide) {
                return;
            }

            const auto timed_predecessors = graph.timed_predecessors_by_node.find(state.current_physical);
            if (timed_predecessors == graph.timed_predecessors_by_node.end()) {
                return;
            }

            for (const auto edge : timed_predecessors->second) {
                if (edge.predecessor.kind != EndpointKind::Stop) {
                    continue;
                }

                visitor(ResidualPredecessorTransition{
                      .predecessor = ResidualReachabilityKey{
                            .current_physical    = edge.predecessor
                          , .phase               = SearchBranchPhase::BeforeFirstBoarding
                          , .remaining_transfers = state.remaining_transfers
                        }
                    , .run_time    = edge.run_time
                    , .kind        = ResidualTransitionKind::FirstTimedRide
                });

                if (state.remaining_transfers < max_transfers) {
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTimedRide
                              , .remaining_transfers = TransferCount{
                                    state.remaining_transfers.get() + 1
                                }
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::TransferTimedRide
                    });
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTransferWalk
                              , .remaining_transfers = TransferCount{
                                    state.remaining_transfers.get() + 1
                                }
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::TransferTimedRide
                    });
                }
            }
        }

        [[nodiscard]] double residual_transition_journey_time(
            const ResidualPredecessorTransition& transition
        ) noexcept {
            return transition.run_time.value();
        }

        [[nodiscard]] double residual_transition_transfer_count(
            const ResidualPredecessorTransition& transition
        ) noexcept {
            return transition.kind == ResidualTransitionKind::TransferTimedRide ? 1.0 : 0.0;
        }

        [[nodiscard]] double residual_transition_impedance(
              const ResidualPredecessorTransition& transition
            , const SearchImpedance&               impedance
            , double                               fare_scale
        ) noexcept {
            ConnectionImpedanceComponents components{};
            switch (transition.kind) {
                case ResidualTransitionKind::AccessWalk:
                    components.access_time = transition.run_time;
                    break;
                case ResidualTransitionKind::TransferWalk:
                    components.transfer_walk_time = transition.run_time;
                    break;
                case ResidualTransitionKind::EgressWalk:
                    components.egress_time = transition.run_time;
                    break;
                case ResidualTransitionKind::FirstTimedRide:
                    components.in_vehicle_time = transition.run_time;
                    break;
                case ResidualTransitionKind::TransferTimedRide:
                    components.in_vehicle_time = transition.run_time;
                    components.transfer_count  = TransferCount{ 1 };
                    break;
            }
            return connection_impedance_value(components, impedance, fare_scale);
        }

        using ResidualDistanceMap = std::unordered_map<
              ResidualReachabilityKey
            , double
            , ResidualReachabilityKeyHash
        >;

        struct ResidualDistanceQueueItem final {
            double                  distance{};
            ResidualReachabilityKey state{};
        };

        struct ResidualDistanceQueueGreater final {
            bool operator()(
                  const ResidualDistanceQueueItem& lhs
                , const ResidualDistanceQueueItem& rhs
            ) const noexcept {
                return lhs.distance > rhs.distance;
            }
        };

        template <typename EdgeCost>
        [[nodiscard]] ResidualDistanceMap compute_residual_suffix_distances(
              const ResidualReverseGraph& graph
            , ZoneId                      destination
            , TransferCount               max_transfers
            , EdgeCost&&                  edge_cost
        ) {
            ResidualDistanceMap distances;
            std::priority_queue<
                  ResidualDistanceQueueItem
                , std::vector<ResidualDistanceQueueItem>
                , ResidualDistanceQueueGreater
            > frontier;

            const auto destination_endpoint = endpoint_key(destination);
            for (std::int32_t remaining = 0; remaining <= max_transfers.get(); ++remaining) {
                const auto seed = ResidualReachabilityKey{
                      .current_physical    = destination_endpoint
                    , .phase               = SearchBranchPhase::Completed
                    , .remaining_transfers = TransferCount{ remaining }
                };
                distances.emplace(seed, 0.0);
                frontier.push(ResidualDistanceQueueItem{
                      .distance = 0.0
                    , .state    = seed
                });
            }

            while (!frontier.empty()) {
                const auto item = frontier.top();
                frontier.pop();

                const auto current_it = distances.find(item.state);
                if (current_it == distances.end() || item.distance != current_it->second) {
                    continue;
                }

                for_each_residual_predecessor_transition(
                      graph
                    , item.state
                    , max_transfers
                    , [&](const ResidualPredecessorTransition& transition) {
                        const auto candidate =
                            item.distance + edge_cost(transition);
                        const auto known = distances.find(transition.predecessor);
                        if (known != distances.end() && known->second <= candidate) {
                            return;
                        }
                        distances[transition.predecessor] = candidate;
                        frontier.push(ResidualDistanceQueueItem{
                              .distance = candidate
                            , .state    = transition.predecessor
                        });
                    }
                );
            }

            return distances;
        }

        [[nodiscard]] std::unordered_map<
              ResidualReachabilityKey
            , ResidualSuffixLowerBounds
            , ResidualReachabilityKeyHash
        > build_residual_suffix_lower_bounds(
              const ResidualReverseGraph& graph
            , ZoneId                      destination
            , TransferCount               max_transfers
            , const SearchImpedance&      impedance
            , double                      fare_scale
        ) {
            const auto journey_time_distances = compute_residual_suffix_distances(
                  graph
                , destination
                , max_transfers
                , [](const ResidualPredecessorTransition& transition) {
                    return residual_transition_journey_time(transition);
                }
            );
            const auto transfer_distances = compute_residual_suffix_distances(
                  graph
                , destination
                , max_transfers
                , [](const ResidualPredecessorTransition& transition) {
                    return residual_transition_transfer_count(transition);
                }
            );
            const auto impedance_distances = compute_residual_suffix_distances(
                  graph
                , destination
                , max_transfers
                , [&](const ResidualPredecessorTransition& transition) {
                    return residual_transition_impedance(
                          transition
                        , impedance
                        , fare_scale
                    );
                }
            );

            std::unordered_map<
                  ResidualReachabilityKey
                , ResidualSuffixLowerBounds
                , ResidualReachabilityKeyHash
            > lower_bounds;
            lower_bounds.reserve(journey_time_distances.size());
            for (const auto& [state, journey_time] : journey_time_distances) {
                const auto transfers = transfer_distances.find(state);
                const auto imp       = impedance_distances.find(state);
                if (transfers == transfer_distances.end() || imp == impedance_distances.end()) {
                    continue;
                }
                lower_bounds.emplace(
                      state
                    , ResidualSuffixLowerBounds{
                          .journey_time = Time{ journey_time }
                        , .transfers    = TransferCount{
                              static_cast<std::int32_t>(transfers->second)
                          }
                        , .impedance    = imp->second
                      }
                );
            }
            return lower_bounds;
        }

        [[nodiscard]] DestinationResidualReachability build_destination_residual_reachability(
              const ResidualReverseGraph& graph
            , ZoneId                      destination
            , TransferCount               max_transfers
            , const SearchImpedance&      impedance
            , double                      fare_scale
        ) {
            DestinationResidualReachability reachability;
            std::deque<ResidualReachabilityKey> frontier;
            const auto destination_endpoint = endpoint_key(destination);

            for (std::int32_t remaining = 0; remaining <= max_transfers.get(); ++remaining) {
                enqueue_reachable_state(
                      reachability
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = destination_endpoint
                        , .phase               = SearchBranchPhase::Completed
                        , .remaining_transfers = TransferCount{ remaining }
                      }
                );
            }

            while (!frontier.empty()) {
                const auto state = frontier.front();
                frontier.pop_front();

                enqueue_walk_predecessors(
                      graph
                    , reachability
                    , frontier
                    , state
                );
                enqueue_timed_predecessors(
                      graph
                    , reachability
                    , frontier
                    , state
                    , max_transfers
                );
            }

            reachability.suffix_lower_bounds = build_residual_suffix_lower_bounds(
                  graph
                , destination
                , max_transfers
                , impedance
                , fare_scale
            );

            return reachability;
        }

        [[nodiscard]] ResidualReachability build_residual_reachability(
              const ResidualReverseGraph& graph
            , std::span<const SearchCompletionTarget> targets
            , TransferCount               max_transfers
            , const SearchImpedance&      impedance
            , double                      fare_scale
        ) {
            ResidualReachability reachability;

            for (const auto& target : targets) {
                const auto destination = target.destination;
                if (reachability.destinations.contains(destination)) {
                    continue;
                }
                reachability.destinations.emplace(
                      destination
                    , build_destination_residual_reachability(
                          graph
                        , destination
                        , max_transfers
                        , impedance
                        , fare_scale
                    )
                );
            }

            return reachability;
        }

        mathfp::Expected<mathfp::Unit> validate_destination_residual_reachability(
              ZoneId                                destination
            , const DestinationResidualReachability& reachability
            , TransferCount                         max_transfers
        ) {
            const auto destination_endpoint = endpoint_key(destination);

            for (std::int32_t remaining = 0; remaining <= max_transfers.get(); ++remaining) {
                if (!has_reachable_state(
                      reachability
                    , ResidualReachabilityKey{
                          .current_physical    = destination_endpoint
                        , .phase               = SearchBranchPhase::Completed
                        , .remaining_transfers = TransferCount{ remaining }
                      }
                )) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability misses destination completion seed")
                            .ctx("destination", destination.get())
                            .ctx("remaining_transfers", remaining)
                    );
                }
            }

            for (const auto& state : reachability.reachable_states) {
                if (state.remaining_transfers.get() < 0
                    || state.remaining_transfers.get() > max_transfers.get()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains state outside transfer budget")
                            .ctx("destination", destination.get())
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                            .ctx("max_transfers", max_transfers.get())
                    );
                }

                if (state.phase == SearchBranchPhase::Completed
                    && state.current_physical != destination_endpoint) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains non-destination completed state")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                    );
                }

                if (state.phase == SearchBranchPhase::AtOrigin
                    && state.current_physical.kind != EndpointKind::Zone) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains at-origin state outside zone")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }

                if ((state.phase == SearchBranchPhase::BeforeFirstBoarding
                        || state.phase == SearchBranchPhase::AfterTimedRide
                        || state.phase == SearchBranchPhase::AfterTransferWalk)
                    && state.current_physical.kind != EndpointKind::Stop) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains stop-phase state outside stop")
                            .ctx("destination", destination.get())
                            .ctx("phase", static_cast<std::int64_t>(state.phase))
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }

                if (state.phase == SearchBranchPhase::Completed
                    && state.current_physical.kind != EndpointKind::Zone) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability contains completed state outside zone")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }

                if (state.remaining_transfers < max_transfers) {
                    const auto relaxed_more_budget_state = ResidualReachabilityKey{
                          .current_physical    = state.current_physical
                        , .phase               = state.phase
                        , .remaining_transfers = TransferCount{
                              state.remaining_transfers.get() + 1
                          }
                    };
                    if (!has_reachable_state(reachability, relaxed_more_budget_state)) {
                        return mathfp::unexpected(
                            mathfp::internal_error("residual reachability violates transfer-budget monotonicity")
                                .ctx("destination", destination.get())
                                .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                                .ctx("endpoint_id", state.current_physical.id)
                                .ctx("remaining_transfers", state.remaining_transfers.get())
                        );
                    }
                }

                const auto lower_bounds = reachability.suffix_lower_bounds.find(state);
                if (lower_bounds == reachability.suffix_lower_bounds.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability state misses suffix lower bounds")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }
                if (
                       !std::isfinite(lower_bounds->second.journey_time.value())
                    || !std::isfinite(lower_bounds->second.impedance)
                    || lower_bounds->second.journey_time.value() < 0.0
                    || lower_bounds->second.impedance < 0.0
                    || lower_bounds->second.transfers.get() < 0
                ) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual suffix lower bounds contain invalid value")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                    );
                }
            }

            for (const auto& [state, lower_bounds] : reachability.suffix_lower_bounds) {
                (void)lower_bounds;
                if (!has_reachable_state(reachability, state)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual suffix lower bounds contain unreachable state")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<mathfp::Unit> validate_residual_reachability(
              const ResidualReachability& reachability
            , TransferCount               max_transfers
        ) {
            for (const auto& [destination, destination_reachability] : reachability.destinations) {
                MATHFP_TRY(validate_destination_residual_reachability(
                      destination
                    , destination_reachability
                    , max_transfers
                ));
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] ReachabilityDecision evaluate_residual_reachability(
              const ResidualReachability& reachability
            , const RelaxedSuffixState&   state
            , TransferCount               max_transfers
        ) noexcept {
            const auto destination_it = reachability.destinations.find(state.destination);
            if (destination_it == reachability.destinations.end()) {
                return ReachabilityDecision{ .feasible = true };
            }

            if (is_completed(state.phase)) {
                const auto feasible = state.current_physical.kind == EndpointKind::Zone
                    && state.current_physical.id == state.destination.get();
                return ReachabilityDecision{
                      .feasible = feasible
                    , .rejection_reason = feasible
                        ? ReachabilityRejectionReason::UnreachableDestination
                        : ReachabilityRejectionReason::Phase
                };
            }

            const auto key = residual_reachability_key(state);
            const auto& destination_reachability = destination_it->second;
            if (has_reachable_state(destination_reachability, key)) {
                return ReachabilityDecision{ .feasible = true };
            }

            if (has_more_budget_state(destination_reachability, key, max_transfers)) {
                return ReachabilityDecision{
                      .feasible = false
                    , .rejection_reason = ReachabilityRejectionReason::TransferBudget
                };
            }

            if (has_any_phase_state_at_endpoint(destination_reachability, state.current_physical)) {
                return ReachabilityDecision{
                      .feasible = false
                    , .rejection_reason = ReachabilityRejectionReason::Phase
                };
            }

            return ReachabilityDecision{
                  .feasible = false
                , .rejection_reason = ReachabilityRejectionReason::UnreachableDestination
            };
        }

        [[nodiscard]] bool can_have_feasible_suffix(
              const ResidualReachability& reachability
            , const RelaxedSuffixState&   state
            , TransferCount               max_transfers
        ) noexcept {
            return evaluate_residual_reachability(
                  reachability
                , state
                , max_transfers
            ).feasible;
        }

        [[nodiscard]] bool can_have_feasible_suffix(
              const ResidualReachability& reachability
            , const SearchBranch&         branch
            , const SearchTask&           task
            , const TransferLimits&       limits
        ) noexcept {
            return can_have_feasible_suffix(
                  reachability
                , relaxed_suffix_state(branch, task, limits)
                , limits.max_transfers
            );
        }

        [[nodiscard]] Time partial_journey_time(
            const SearchPartialMetrics& metrics
        ) noexcept {
            return Time{
                metrics.current_time->value() - metrics.departure->value()
            };
        }

        [[nodiscard]] Time partial_walk_time(
            const SearchPartialMetrics& metrics
        ) noexcept {
            return metrics.access_time + metrics.transfer_walk_time + metrics.egress_time;
        }

        [[nodiscard]] ConnectionImpedanceComponents partial_impedance_components(
            const SearchPartialMetrics& metrics
        ) noexcept {
            return ConnectionImpedanceComponents{
                  .in_vehicle_time    = metrics.in_vehicle_time
                , .access_time        = metrics.access_time
                , .egress_time        = metrics.egress_time
                , .transfer_walk_time = metrics.transfer_walk_time
                , .transfer_wait_time = metrics.transfer_wait_time
                , .transfer_count     = metrics.transfers
                , .fare               = metrics.fare
            };
        }

        SearchPruningTransferContext search_transfer_context(
            const SearchBranch& branch
        ) noexcept {
            return SearchPruningTransferContext{
                  .last_trip = branch.trace.last_timed_segment != nullptr
                    ? branch.trace.last_timed_segment->trip
                    : std::optional<TripId>{}
                , .last_line = branch.trace.last_timed_route_segment != nullptr
                    ? line_of(*branch.trace.last_timed_route_segment)
                    : std::optional<LineId>{}
            };
        }

        std::optional<StopOccurrenceKey> search_last_timed_occurrence(
            const SearchBranch& branch
        ) noexcept {
            if (branch.trace.last_timed_route_segment == nullptr) {
                return std::nullopt;
            }
            return occurrence_key(
                line_topology_of(*branch.trace.last_timed_route_segment)->to
            );
        }

        SearchPruningStateProjection search_pruning_state_projection(
            const SearchBranch& branch
        ) noexcept {
            return SearchPruningStateProjection{
                  .physical              = branch.trace.current_physical
                , .current_occurrence    = branch.trace.current_occurrence
                , .last_timed_occurrence = search_last_timed_occurrence(branch)
                , .phase                 = branch.trace.phase
                , .transfer              = search_transfer_context(branch)
            };
        }

        SearchNodeKey search_node_key(
              const SearchBranch&               branch
            , const SearchPruningExecutionPlan& pruning_execution
        ) noexcept {
            return make_search_pruning_state_key(
                  search_pruning_state_projection(branch)
                , pruning_execution.equivalent_connection_dominance
            );
        }

        bool branch_revisits_physical(
              const BranchArena&  branches
            , const SearchBranch& branch
            , EndpointKey         next
        ) {
            if (branch.trace.current_physical == next) {
                return true;
            }

            auto cursor = branch.trace.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branch_at(branches, *cursor);
                if (ancestor.trace.current_physical == next) {
                    return true;
                }
                cursor = ancestor.trace.parent_branch;
            }

            return false;
        }

        bool branch_revisits_occurrence(
              const BranchArena&  branches
            , const SearchBranch& branch
            , StopOccurrenceKey   next
        ) {
            if (branch.trace.current_occurrence.has_value() && branch.trace.current_occurrence.value() == next) {
                return true;
            }

            auto cursor = branch.trace.parent_branch;
            while (cursor.has_value()) {
                const auto& ancestor = branch_at(branches, *cursor);
                if (ancestor.trace.current_occurrence.has_value() && ancestor.trace.current_occurrence.value() == next) {
                    return true;
                }
                cursor = ancestor.trace.parent_branch;
            }

            return false;
        }

        bool is_complete_connection(
              const SearchBranch& branch
            , ZoneId              task_destination
        ) noexcept {
            return branch.metrics.departure.has_value()
                && branch.trace.current_physical.kind == EndpointKind::Zone
                && branch.trace.current_physical.id   == task_destination.get();
        }

        [[nodiscard]] SearchBranchPhase search_branch_phase(
              const SearchBranch& branch
            , ZoneId              task_destination
        ) noexcept {
            if (is_complete_connection(branch, task_destination)) {
                return SearchBranchPhase::Completed;
            }
            return branch.trace.phase;
        }

        [[nodiscard]] TransferCount remaining_transfer_budget(
              const SearchBranch&   branch
            , const TransferLimits& limits
        ) noexcept {
            const auto used = branch.metrics.departure.has_value()
                ? branch.metrics.transfers.get()
                : 0;
            return TransferCount{
                std::max(0, limits.max_transfers.get() - used)
            };
        }

        [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
              const SearchBranch&   branch
            , ZoneId                destination
            , const TransferLimits& limits
        ) noexcept {
            return RelaxedSuffixState{
                  .current_physical    = branch.trace.current_physical
                , .destination         = destination
                , .phase               = search_branch_phase(branch, destination)
                , .remaining_transfers = remaining_transfer_budget(branch, limits)
            };
        }

        [[nodiscard]] RelaxedSuffixState relaxed_suffix_state(
              const SearchBranch&   branch
            , const SearchTask&     task
            , const TransferLimits& limits
        ) noexcept {
            return relaxed_suffix_state(branch, task.destination, limits);
        }

        bool first_timed_departure_allowed(
              const SearchBranch&      branch
            , const ConnectionSegment& successor
            , const SearchTimeDomain*  first_departure_domain
            , const TransferLimits&
        ) noexcept {
            if (branch.metrics.departure.has_value()) {
                return true;
            }
            if (first_departure_domain == nullptr || !successor.departure.has_value()) {
                return true;
            }
            return contains(*first_departure_domain, *successor.departure);
        }

        mathfp::Expected<PartialPruningMetrics> make_partial_pruning_metrics(
              const SearchBranch&    branch
            , const SearchCostContext& search_cost
        ) {
            const auto journey_time = partial_journey_time(branch.metrics);
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(branch.metrics)
                , .capacity_exposure = branch.metrics.capacity_exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(cost_components, search_cost)
            );

            return PartialPruningMetrics{
                  .departure    = *branch.metrics.departure
                , .arrival      = *branch.metrics.current_time
                , .journey_time = journey_time
                , .walk_time    = partial_walk_time(branch.metrics)
                , .transfers    = branch.metrics.transfers
                , .fare         = branch.metrics.fare
                , .impedance    = impedance
            };
        }

        void insert_pruning_metrics(
              NodeMetricMap&                    known_metrics
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchNodeKey                     node
            , PartialPruningMetrics             metrics
        ) {
            auto& known = known_metrics[node];
            insert_search_pruning_metrics_in_place(
                  pruning_execution
                , known
                , std::move(metrics)
            );
        }

        void insert_pruning_metrics(
              SearchProjectionRetention&        retention
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchNodeKey                     node
            , PartialPruningMetrics             metrics
        ) {
            insert_pruning_metrics(
                  retention.known_metrics
                , pruning_execution
                , std::move(node)
                , std::move(metrics)
            );
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const CompleteConnectionRetention& retention
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = retention.alternatives.empty()
            };
            for (const auto& alternative : retention.alternatives) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , alternative.metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , alternative.metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(alternative.metrics.transfers.get())
                );
            }
            return summary;
        }

        [[nodiscard]] CompleteConnectionMetricSummary summarize_complete_metrics(
            const CompactCompleteConnectionRetention& retention
        ) noexcept {
            CompleteConnectionMetricSummary summary{
                  .min_impedance    = std::numeric_limits<double>::infinity()
                , .min_journey_time = std::numeric_limits<double>::infinity()
                , .min_transfers    = std::numeric_limits<double>::infinity()
                , .empty            = retention.metrics.empty()
            };
            for (const auto& metrics : retention.metrics) {
                summary.min_impedance = std::min(
                      summary.min_impedance
                    , metrics.impedance
                );
                summary.min_journey_time = std::min(
                      summary.min_journey_time
                    , metrics.journey_time.value()
                );
                summary.min_transfers = std::min(
                      summary.min_transfers
                    , static_cast<double>(metrics.transfers.get())
                );
            }
            return summary;
        }

        struct CompletionMetricLowerBound final {
            std::optional<Time> departure{};
            std::optional<Time> arrival{};
            Time                journey_time{};
            double              transfers{};
            double              impedance{};
        };

        struct SuffixLowerBoundPruningDecision final {
            bool                            feasible{ true };
            SuffixLowerBoundRejectionReason rejection_reason{
                SuffixLowerBoundRejectionReason::ToleranceImpedance
            };
        };

        [[nodiscard]] mathfp::Expected<double> partial_impedance_value(
              const SearchBranch&    branch
            , const SearchCostContext& search_cost
        ) {
            const auto cost_components = SearchCostComponents{
                  .base = partial_impedance_components(branch.metrics)
                , .capacity_exposure = branch.metrics.capacity_exposure
            };
            return search_impedance(
                  cost_components
                , search_cost
            );
        }

        [[nodiscard]] Time partial_journey_time_lower_bound(
            const SearchBranch& branch
        ) noexcept {
            if (branch.metrics.departure.has_value()) {
                return partial_journey_time(branch.metrics);
            }
            return branch.metrics.access_time;
        }

        [[nodiscard]] double suffix_capacity_impedance_lower_bound(
            const SearchCostContext& search_cost
        ) noexcept {
            switch (search_cost.mode) {
                case SearchCostMode::BaseOnly:
                    return 0.0;

                case SearchCostMode::CapacityAware:
                    /*
                     * Supported capacity penalties are non-negative for valid
                     * load/capacity ratios, and the volume-capacity weight is
                     * validated as non-negative. Therefore zero is an
                     * admissible lower bound for the unknown suffix capacity
                     * term. This may weaken suffix pruning, but cannot reject
                     * a completion that could become feasible under the full
                     * capacity-aware search impedance.
                     */
                    return 0.0;
            }

            return 0.0;
        }

        [[nodiscard]] mathfp::Expected<CompletionMetricLowerBound> completion_metric_lower_bound(
              const SearchBranch&                branch
            , const ResidualSuffixLowerBounds& suffix
            , const SearchCostContext&           search_cost
        ) {
            MATHFP_TRY_LET(
                  double
                , partial_impedance
                , partial_impedance_value(branch, search_cost)
            );
            const auto suffix_capacity_impedance =
                suffix_capacity_impedance_lower_bound(search_cost);
            return CompletionMetricLowerBound{
                  .departure   = branch.metrics.departure
                , .arrival     = branch.metrics.current_time.has_value()
                    ? std::optional<Time>{
                        Time{ branch.metrics.current_time->value() + suffix.journey_time.value() }
                    }
                    : std::nullopt
                , .journey_time = Time{
                      partial_journey_time_lower_bound(branch).value()
                    + suffix.journey_time.value()
                  }
                , .transfers    = static_cast<double>(branch.metrics.transfers.get())
                    + static_cast<double>(suffix.transfers.get())
                , .impedance    = partial_impedance
                    + suffix.impedance
                    + suffix_capacity_impedance
            };
        }

        [[nodiscard]] bool complete_connection_dominates_completion_lower_bound(
              const CompleteConnectionDominanceConfig& dominance_config
            , const CompleteConnectionMetrics&         complete
            , const CompletionMetricLowerBound&        lower_bound
        ) noexcept {
            if (!complete_connection_can_dominate(dominance_config, complete)) {
                return false;
            }
            if (!lower_bound.departure.has_value() || !lower_bound.arrival.has_value()) {
                return false;
            }

            const auto no_worse =
                   complete.departure.value() >= lower_bound.departure->value()
                && complete.arrival.value()   <= lower_bound.arrival->value()
                && complete.impedance         <= lower_bound.impedance
                && static_cast<double>(complete.transfers.get()) <= lower_bound.transfers;

            const auto strictly_better =
                   complete.departure.value() > lower_bound.departure->value()
                || complete.arrival.value()   < lower_bound.arrival->value()
                || complete.impedance         < lower_bound.impedance
                || static_cast<double>(complete.transfers.get()) < lower_bound.transfers;

            return no_worse && strictly_better;
        }

        [[nodiscard]] bool violates_complete_tolerance_lower_bound(
              const CompletionMetricLowerBound&     lower_bound
            , const CompleteConnectionMetricSummary& summary
            , const ChoiceTolerances&               tolerances
            , SuffixLowerBoundRejectionReason&      reason
        ) noexcept {
            if (summary.empty) {
                return false;
            }

            const auto impedance_bound =
                  mathfp::units::as_dimless(tolerances.imp_mult)
                * summary.min_impedance
                + mathfp::units::as_dimless(tolerances.imp_add);
            if (lower_bound.impedance > impedance_bound) {
                reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
                return true;
            }

            const auto journey_time_bound =
                  mathfp::units::as_dimless(tolerances.jt_mult)
                * summary.min_journey_time
                + mathfp::units::as_dimless(tolerances.jt_add);
            if (lower_bound.journey_time.value() > journey_time_bound) {
                reason = SuffixLowerBoundRejectionReason::ToleranceJourneyTime;
                return true;
            }

            const auto transfer_bound =
                  mathfp::units::as_dimless(tolerances.nt_mult)
                * summary.min_transfers
                + mathfp::units::as_dimless(tolerances.nt_add);
            if (lower_bound.transfers > transfer_bound) {
                reason = SuffixLowerBoundRejectionReason::ToleranceTransfers;
                return true;
            }

            return false;
        }

        [[nodiscard]] mathfp::Expected<SuffixLowerBoundPruningDecision> evaluate_suffix_lower_bound_pruning(
              const SearchBranch&                branch
            , ZoneId                             destination
            , const ResidualReachability&        reachability
            , const CompleteConnectionRetention& complete_retention
            , const SearchParams&                params
            , const SearchCostContext&           search_cost
            , const ChoiceConfig&                choice_config
            , const CompleteConnectionDominanceConfig& dominance_config
        ) {
            if (complete_retention.alternatives.empty()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto destination_it = reachability.destinations.find(destination);
            if (destination_it == reachability.destinations.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto state = residual_reachability_key(
                relaxed_suffix_state(branch, destination, params.transfers)
            );
            const auto lower_bound_it = destination_it->second.suffix_lower_bounds.find(state);
            if (lower_bound_it == destination_it->second.suffix_lower_bounds.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            MATHFP_TRY_LET(
                  CompletionMetricLowerBound
                , lower_bound
                , completion_metric_lower_bound(
                      branch
                    , lower_bound_it->second
                    , search_cost
                )
            );

            for (const auto& complete : complete_retention.alternatives) {
                if (complete_connection_dominates_completion_lower_bound(
                      dominance_config
                    , complete.metrics
                    , lower_bound
                )) {
                    return SuffixLowerBoundPruningDecision{
                          .feasible = false
                        , .rejection_reason = SuffixLowerBoundRejectionReason::ExactDominance
                    };
                }
            }

            if (choice_config.rollout_stage != ChoiceRolloutStage::ExactAndApproximate) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            auto reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
            if (violates_complete_tolerance_lower_bound(
                  lower_bound
                , summarize_complete_metrics(complete_retention)
                , params.choice_tolerances
                , reason
            )) {
                return SuffixLowerBoundPruningDecision{
                      .feasible = false
                    , .rejection_reason = reason
                };
            }

            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        [[nodiscard]] mathfp::Expected<SuffixLowerBoundPruningDecision> evaluate_suffix_lower_bound_pruning(
              const SearchBranch&                       branch
            , ZoneId                                    destination
            , const ResidualReachability&               reachability
            , const CompactCompleteConnectionRetention& complete_retention
            , const SearchParams&                       params
            , const SearchCostContext&                  search_cost
            , const ChoiceConfig&                       choice_config
            , const CompleteConnectionDominanceConfig&  dominance_config
        ) {
            if (complete_retention.metrics.empty()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto destination_it = reachability.destinations.find(destination);
            if (destination_it == reachability.destinations.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            const auto state = residual_reachability_key(
                relaxed_suffix_state(branch, destination, params.transfers)
            );
            const auto lower_bound_it = destination_it->second.suffix_lower_bounds.find(state);
            if (lower_bound_it == destination_it->second.suffix_lower_bounds.end()) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            MATHFP_TRY_LET(
                  CompletionMetricLowerBound
                , lower_bound
                , completion_metric_lower_bound(
                      branch
                    , lower_bound_it->second
                    , search_cost
                )
            );

            for (const auto& complete : complete_retention.metrics) {
                if (complete_connection_dominates_completion_lower_bound(
                      dominance_config
                    , complete
                    , lower_bound
                )) {
                    return SuffixLowerBoundPruningDecision{
                          .feasible = false
                        , .rejection_reason = SuffixLowerBoundRejectionReason::ExactDominance
                    };
                }
            }

            if (choice_config.rollout_stage != ChoiceRolloutStage::ExactAndApproximate) {
                return SuffixLowerBoundPruningDecision{ .feasible = true };
            }

            auto reason = SuffixLowerBoundRejectionReason::ToleranceImpedance;
            if (violates_complete_tolerance_lower_bound(
                  lower_bound
                , summarize_complete_metrics(complete_retention)
                , params.choice_tolerances
                , reason
            )) {
                return SuffixLowerBoundPruningDecision{
                      .feasible = false
                    , .rejection_reason = reason
                };
            }

            return SuffixLowerBoundPruningDecision{ .feasible = true };
        }

        std::optional<std::pair<std::size_t, std::size_t>> timed_bucket_range(
              const preprocessing::ConnectionSegmentIndex& index
            , StopOccurrenceKey                            from
        ) {
            const auto bucket = preprocessing::find_bucket(index.timed_buckets, from);
            if (!bucket) {
                return std::nullopt;
            }

            const auto i = *bucket;
            return std::pair<std::size_t, std::size_t>{
                  index.timed_offsets[i]
                , index.timed_offsets[i + 1]
            };
        }

        template <typename Visitor>
        void for_each_boarding_successor_in_window(
              const PreprocessedNetwork& network
            , std::size_t                start
            , std::size_t                end
            , SearchTimeWindow           window
            , Visitor&&                  visit
        ) {
            auto&& visitor = visit;
            const auto departures_begin =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(start);
            const auto departures_end =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(end);
            const auto begin = std::lower_bound(
                  departures_begin
                , departures_end
                , window.begin
                , [](const Time& lhs, const Time& rhs) {
                    return lhs.value() < rhs.value();
                }
            );

            auto idx = start + static_cast<std::size_t>(begin - departures_begin);
            for (auto it = begin; it != departures_end; ++it, ++idx) {
                if (it->value() > window.end.value()) {
                    break;
                }
                visitor(network.connection_index.boarding_order[idx]);
            }
        }

        template <typename Visitor>
        void for_each_timed_successor(
              const PreprocessedNetwork& network
            , EndpointKey                physical_from
            , std::optional<Time>        current_time
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
            , Visitor&&                  visit
        ) {
            auto&& visitor = visit;
            if (physical_from.kind != EndpointKind::Stop) {
                return;
            }

            const auto bucket = preprocessing::find_bucket(
                  network.connection_index.boarding_stop_buckets
                , StopId{ physical_from.id }
            );
            if (!bucket) {
                return;
            }

            const auto bucket_index = *bucket;
            const auto start        = network.connection_index.boarding_offsets[bucket_index];
            const auto end          = network.connection_index.boarding_offsets[bucket_index + 1];
            if (start >= end) {
                return;
            }

            if (!current_time.has_value()) {
                if (first_departure_domain != nullptr) {
                    for (const auto& window : first_departure_domain->windows) {
                        for_each_boarding_successor_in_window(
                              network
                            , start
                            , end
                            , window
                            , visitor
                        );
                    }
                    return;
                }
                for (std::size_t i = start; i < end; ++i) {
                    visitor(network.connection_index.boarding_order[i]);
                }
                return;
            }

            const auto earliest = Time{
                current_time->value() + limits.min_transfer_wait.value()
            };
            const auto latest = Time{
                current_time->value() + limits.max_transfer_wait.value()
            };
            const auto departures_begin =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(start);
            const auto departures_end =
                network.connection_index.boarding_departures.begin() + static_cast<std::ptrdiff_t>(end);
            const auto begin = std::lower_bound(
                  departures_begin
                , departures_end
                , earliest
                , [](const Time& lhs, const Time& rhs) {
                    return lhs.value() < rhs.value();
                }
            );

            auto idx = start + static_cast<std::size_t>(begin - departures_begin);
            for (auto it = begin; it != departures_end; ++it, ++idx) {
                if (it->value() > latest.value()) {
                    break;
                }
                visitor(network.connection_index.boarding_order[idx]);
            }
        }

        bool is_same_line_transfer_candidate(
              const SearchBranch&      branch
            , const ConnectionSegment& successor
            , const RouteSegment&      successor_route_segment
        ) noexcept {
            if (!branch.trace.last_timed_segment || !branch.trace.last_timed_route_segment) {
                return false;
            }
            if (!is_timed_connection(successor)) {
                return false;
            }
            if (!same_line(*branch.trace.last_timed_route_segment, successor_route_segment)) {
                return false;
            }
            return successor.trip != branch.trace.last_timed_segment->trip;
        }

        bool is_repeated_stop_reboarding_case(
              const SearchBranch&      branch
            , const ConnectionSegment& successor
            , const RouteSegment&      successor_route_segment
        ) noexcept {
            if (!is_same_line_transfer_candidate(branch, successor, successor_route_segment)) {
                return false;
            }
            const auto* current_line   = line_topology_of(*branch.trace.last_timed_route_segment);
            const auto* successor_line = line_topology_of(successor_route_segment);
            if (!current_line || !successor_line) {
                return false;
            }
            if (current_line->to.stop != successor_line->from.stop) {
                return false;
            }
            if (!branch.trace.last_timed_segment->to_index.has_value() || !successor.from_index.has_value()) {
                return false;
            }
            return successor.from_index.value() < branch.trace.last_timed_segment->to_index.value();
        }

        std::optional<Time> same_trip_continuation_arrival(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , const RouteSegment&        successor_route_segment
        ) noexcept {
            if (!branch.trace.last_timed_segment || !branch.trace.last_timed_route_segment) {
                return std::nullopt;
            }
            if (
                   !branch.trace.last_timed_segment->trip    .has_value()
                || !branch.trace.last_timed_segment->to_index.has_value()
                || !successor.to_index                    .has_value()
            ) {
                return std::nullopt;
            }

            const auto current_stop           = occurrence_key(line_topology_of(*branch.trace.last_timed_route_segment)->to);
            const auto desired_route_to_index = successor.to_index.value();
            const auto range                  = timed_bucket_range(network.connection_index, current_stop);
            if (!range) {
                return std::nullopt;
            }

            const auto [start, end] = *range;
            for (std::size_t i = start; i < end; ++i) {
                const auto  connection_id              = network.connection_index.timed_order[i];
                const auto& continuation               = connection_segment_at(network, connection_id);
                const auto& continuation_route_segment = route_segment_at(
                      network
                    , continuation.route_segment
                );
                if (!same_line(*branch.trace.last_timed_route_segment, continuation_route_segment)) {
                    continue;
                }
                if (continuation.trip != branch.trace.last_timed_segment->trip) {
                    continue;
                }
                if (continuation.from_index != branch.trace.last_timed_segment->to_index) {
                    continue;
                }
                // Repeated-stop geometry must match the same downstream route
                // occurrence, not merely the same physical stop.
                if (continuation.to_index != desired_route_to_index) {
                    continue;
                }
                return continuation.arrival;
            }

            return std::nullopt;
        }

        bool improves_repeated_stop_reboarding(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , const RouteSegment&        successor_route_segment
        ) noexcept {
            if (!is_repeated_stop_reboarding_case(branch, successor, successor_route_segment)) {
                return true;
            }
            const auto continuation_arrival = same_trip_continuation_arrival(
                  branch
                , network
                , successor
                , successor_route_segment
            );
            if (!continuation_arrival.has_value() || !successor.arrival.has_value()) {
                return false;
            }
            return successor.arrival->value() < continuation_arrival->value();
        }

        bool is_active_batch_destination(
              EndpointKey                       endpoint
            , const ActiveIndexSet&             active_targets
            , std::span<const SearchCompletionTarget> batch_targets
        ) noexcept {
            if (endpoint.kind != EndpointKind::Zone) {
                return false;
            }
            bool active = false;
            active_targets.for_each_index([&](std::size_t target_pos) {
                if (batch_targets[target_pos].destination.get() == endpoint.id) {
                    active = true;
                }
            });
            return active;
        }

        struct WalkExtensionTransition final {
            ConnectionLegKind kind{ ConnectionLegKind::AccessWalk };
            SearchBranchPhase next_phase{ SearchBranchPhase::BeforeFirstBoarding };
        };

        std::optional<WalkExtensionTransition> walk_extension_transition_table(
              SearchBranchPhase phase
            , EndpointKey        from
            , EndpointKey        to
            , bool               active_destination
        ) noexcept {
            switch (phase) {
                case SearchBranchPhase::AtOrigin:
                    if (from.kind == EndpointKind::Zone
                        && to.kind == EndpointKind::Stop) {
                        return WalkExtensionTransition{
                              .kind       = ConnectionLegKind::AccessWalk
                            , .next_phase = SearchBranchPhase::BeforeFirstBoarding
                        };
                    }
                    return std::nullopt;

                case SearchBranchPhase::AfterTimedRide:
                    if (from.kind == EndpointKind::Stop
                        && to.kind == EndpointKind::Stop) {
                        return WalkExtensionTransition{
                              .kind       = ConnectionLegKind::TransferWalk
                            , .next_phase = SearchBranchPhase::AfterTransferWalk
                        };
                    }
                    if (from.kind == EndpointKind::Stop
                        && to.kind == EndpointKind::Zone
                        && active_destination) {
                        return WalkExtensionTransition{
                              .kind       = ConnectionLegKind::EgressWalk
                            , .next_phase = SearchBranchPhase::Completed
                        };
                    }
                    return std::nullopt;

                case SearchBranchPhase::BeforeFirstBoarding:
                case SearchBranchPhase::AfterTransferWalk:
                case SearchBranchPhase::Completed:
                    return std::nullopt;
            }

            return std::nullopt;
        }

        std::optional<WalkExtensionTransition> admissible_walk_extension_transition(
              ZoneId                         origin
            , const ActiveIndexSet&          active_targets
            , std::span<const SearchCompletionTarget> batch_targets
            , const SearchBranch&            branch
            , const RouteSegment&            route_segment
        ) noexcept {
            const auto from_physical = branch.trace.current_physical;
            const auto next_physical = physical_to_key(route_segment);

            if (branch.trace.phase == SearchBranchPhase::AtOrigin
                && (from_physical.kind != EndpointKind::Zone
                    || from_physical.id != origin.get())) {
                return std::nullopt;
            }

            return walk_extension_transition_table(
                  branch.trace.phase
                , from_physical
                , next_physical
                , is_active_batch_destination(
                      next_physical
                    , active_targets
                    , batch_targets
                  )
            );
        }

        [[nodiscard]] Time add_time(
              Time lhs
            , Time rhs
        ) noexcept {
            return Time{ lhs.value() + rhs.value() };
        }

        [[nodiscard]] ConnectionLeg make_walk_leg(
              ConnectionLegKind   kind
            , ConnectionSegmentId segment_id
            , const RouteSegment& route_segment
            , Time                start_time
        ) {
            return ConnectionLeg{
                  .kind               = kind
                , .connection_segment = segment_id
                , .route_segment      = route_segment.id
                , .physical_from      = physical_from_key(route_segment)
                , .physical_to        = physical_to_key(route_segment)
                , .occurrence_from    = std::nullopt
                , .occurrence_to      = std::nullopt
                , .line               = std::nullopt
                , .route              = std::nullopt
                , .trip               = std::nullopt
                , .start_time         = start_time
                , .end_time           = add_time(start_time, route_segment.run_time)
                , .length             = route_segment.length
                , .fare               = 0.0
            };
        }

        [[nodiscard]] std::optional<ConnectionLeg> make_transfer_wait_leg(
              Time                        current_time
            , const ConnectionSegment&    segment
            , EndpointKey                  physical
        ) noexcept {
            if (!segment.departure.has_value()) {
                return std::nullopt;
            }
            if (segment.departure->value() <= current_time.value()) {
                return std::nullopt;
            }
            return ConnectionLeg{
                  .kind               = ConnectionLegKind::TransferWait
                , .connection_segment = std::nullopt
                , .route_segment      = std::nullopt
                , .physical_from      = physical
                , .physical_to        = physical
                , .occurrence_from    = std::nullopt
                , .occurrence_to      = std::nullopt
                , .line               = std::nullopt
                , .route              = std::nullopt
                , .trip               = std::nullopt
                , .start_time         = current_time
                , .end_time           = *segment.departure
                , .length             = Length{ 0.0 }
                , .fare               = 0.0
            };
        }

        [[nodiscard]] std::optional<ConnectionLeg> make_transfer_wait_leg(
              const SearchPartialMetrics& metrics
            , const ConnectionSegment&    segment
            , EndpointKey                  physical
        ) noexcept {
            if (!metrics.current_time.has_value()) {
                return std::nullopt;
            }
            return make_transfer_wait_leg(*metrics.current_time, segment, physical);
        }

        [[nodiscard]] ConnectionLeg make_ride_leg(
              const ConnectionSegment& segment
            , const RouteSegment&      route_segment
        ) {
            const auto* line = line_topology_of(route_segment);
            return ConnectionLeg{
                  .kind               = ConnectionLegKind::Ride
                , .connection_segment = segment.id
                , .route_segment      = route_segment.id
                , .physical_from      = physical_from_key(route_segment)
                , .physical_to        = physical_to_key(route_segment)
                , .occurrence_from    = occurrence_key(line->from)
                , .occurrence_to      = occurrence_key(line->to)
                , .line               = line->line
                , .route              = line->route
                , .trip               = segment.trip
                , .start_time         = *segment.departure
                , .end_time           = *segment.arrival
                , .length             = route_segment.length
                , .fare               = segment.fare.value_or(0.0)
            };
        }

        [[nodiscard]] SearchPartialTrace extend_trace_with_walk(
              SearchPartialTrace            trace
            , std::size_t                    branch_index
            , const RouteSegment&            route_segment
            , ConnectionSegmentId            segment_id
            , const WalkExtensionTransition& transition
        ) {
            const auto next_physical = physical_to_key(route_segment);
            trace.parent_branch      = branch_index;
            trace.incoming_segment   = segment_id;
            trace.current_physical   = next_physical;
            trace.current_occurrence = std::nullopt;
            trace.phase              = transition.next_phase;
            return trace;
        }

        [[nodiscard]] SearchPartialMetrics extend_metrics_with_walk(
              SearchPartialMetrics metrics
            , const RouteSegment&   route_segment
            , ConnectionLegKind     kind
        ) {
            switch (kind) {
                case ConnectionLegKind::AccessWalk:
                    metrics.access_time = Time{
                        metrics.access_time.value() + route_segment.run_time.value()
                    };
                    return metrics;

                case ConnectionLegKind::TransferWalk:
                    metrics.current_time = Time{
                        metrics.current_time->value() + route_segment.run_time.value()
                    };
                    metrics.transfer_walk_time = Time{
                        metrics.transfer_walk_time.value() + route_segment.run_time.value()
                    };
                    return metrics;

                case ConnectionLegKind::EgressWalk:
                    metrics.current_time = Time{
                        metrics.current_time->value() + route_segment.run_time.value()
                    };
                    metrics.egress_time = Time{
                        metrics.egress_time.value() + route_segment.run_time.value()
                    };
                    return metrics;

                case ConnectionLegKind::Ride:
                case ConnectionLegKind::InitialWait:
                case ConnectionLegKind::TransferWait:
                case ConnectionLegKind::FinalWait:
                    return metrics;
            }

            return metrics;
        }

        [[nodiscard]] SearchBranch extend_with_walk(
              const SearchBranch&           branch
            , std::size_t                   branch_index
            , const RouteSegment&           route_segment
            , ConnectionSegmentId           segment_id
            , const WalkExtensionTransition& transition
        ) {
            const auto trace = extend_trace_with_walk(
                  branch.trace
                , branch_index
                , route_segment
                , segment_id
                , transition
            );
            return SearchBranch{
                  .trace   = trace
                , .metrics = extend_metrics_with_walk(
                      branch.metrics
                    , route_segment
                    , transition.kind
                  )
            };
        }

        [[nodiscard]] SearchPartialTrace extend_trace_with_timed(
              SearchPartialTrace       trace
            , std::size_t               branch_index
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
            , SearchBranchPhase         next_phase
        ) {
            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            trace.parent_branch                  = branch_index;
            trace.incoming_segment               = segment.id;
            trace.current_physical               = physical_to_key(route_segment);
            trace.current_occurrence             = next_occurrence;
            trace.phase                          = next_phase;
            trace.last_timed_segment             = &segment;
            trace.last_timed_route_segment       = &route_segment;
            return trace;
        }

        std::optional<SearchBranchPhase> timed_extension_transition(
            SearchBranchPhase phase
        ) noexcept {
            switch (phase) {
                case SearchBranchPhase::BeforeFirstBoarding:
                case SearchBranchPhase::AfterTimedRide:
                case SearchBranchPhase::AfterTransferWalk:
                    return SearchBranchPhase::AfterTimedRide;

                case SearchBranchPhase::AtOrigin:
                case SearchBranchPhase::Completed:
                    return std::nullopt;
            }

            return std::nullopt;
        }

        [[nodiscard]] CapacityExposure add_capacity_exposure(
              CapacityExposure lhs
            , CapacityExposure rhs
        ) noexcept {
            return CapacityExposure{
                Time{
                    lhs.equivalent_time.value()
                    + rhs.equivalent_time.value()
                }
            };
        }

        [[nodiscard]] mathfp::Expected<CapacityExposure> timed_successor_capacity_exposure(
              const ConnectionSegment& segment
            , const RouteSegment&      route_segment
            , std::optional<IntervalId> interval
            , const SearchCostContext& search_cost
        ) {
            switch (search_cost.mode) {
                case SearchCostMode::BaseOnly:
                    return CapacityExposure{ Time{ 0.0 } };

                case SearchCostMode::CapacityAware:
                    if (!interval.has_value()) {
                        return mathfp::unexpected(
                            mathfp::invalid_arg("capacity-aware origin-period search requires task-indexed interval exposure")
                        );
                    }
                    return search_capacity_exposure(
                          make_ride_leg(segment, route_segment)
                        , *interval
                        , search_cost.capacity
                    );
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("unknown search cost mode")
                    .ctx("mode", static_cast<std::int64_t>(search_cost.mode))
            );
        }

        [[nodiscard]] SearchPartialMetrics extend_metrics_with_timed(
              SearchPartialMetrics      metrics
            , const ConnectionSegment& segment
            , CapacityExposure          capacity_exposure
        ) {
            const auto had_departure = metrics.departure.has_value();
            if (!had_departure) {
                metrics.departure = Time{
                    segment.departure->value() - metrics.access_time.value()
                };
            } else {
                metrics.transfer_wait_time = Time{
                    metrics.transfer_wait_time.value()
                    + (segment.departure->value() - metrics.current_time->value())
                };
                metrics.transfers = TransferCount{ metrics.transfers.get() + 1 };
            }

            metrics.in_vehicle_time = Time{
                metrics.in_vehicle_time.value()
                + (segment.arrival->value() - segment.departure->value())
            };
            metrics.current_time = *segment.arrival;
            metrics.fare         = metrics.fare + segment.fare.value_or(0.0);
            metrics.capacity_exposure = add_capacity_exposure(
                  metrics.capacity_exposure
                , capacity_exposure
            );
            return metrics;
        }

        [[nodiscard]] mathfp::Expected<SearchBranch> extend_with_timed(
              const SearchBranch&      branch
            , std::size_t              branch_index
            , const ConnectionSegment& segment
            , const RouteSegment&      route_segment
            , SearchBranchPhase         next_phase
            , std::optional<IntervalId> interval
            , const SearchCostContext& search_cost
        ) {
            MATHFP_TRY_LET(
                  CapacityExposure
                , capacity_exposure
                , timed_successor_capacity_exposure(
                      segment
                    , route_segment
                    , interval
                    , search_cost
                )
            );
            return SearchBranch{
                  .trace   = extend_trace_with_timed(
                        branch.trace
                      , branch_index
                      , segment
                      , route_segment
                      , next_phase
                  )
                , .metrics = extend_metrics_with_timed(
                      branch.metrics
                    , segment
                    , capacity_exposure
                )
            };
        }

        [[nodiscard]] const char* search_branch_phase_name(
            SearchBranchPhase phase
        ) noexcept {
            switch (phase) {
                case SearchBranchPhase::AtOrigin:
                    return "at_origin";

                case SearchBranchPhase::BeforeFirstBoarding:
                    return "before_first_boarding";

                case SearchBranchPhase::AfterTimedRide:
                    return "after_timed_ride";

                case SearchBranchPhase::AfterTransferWalk:
                    return "after_transfer_walk";

                case SearchBranchPhase::Completed:
                    return "completed";
            }

            return "unknown";
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> phase_invariant_error(
              const SearchBranch& branch
            , const char*         reason
        ) {
            return mathfp::unexpected(
                mathfp::internal_error("search branch phase invariant violation")
                    .ctx("reason", std::string(reason))
                    .ctx("phase", std::string(search_branch_phase_name(branch.trace.phase)))
                    .ctx("origin", branch.trace.origin.get())
                    .ctx("current_kind", static_cast<std::int64_t>(branch.trace.current_physical.kind))
                    .ctx("current_id", branch.trace.current_physical.id)
            );
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_search_branch_phase_invariants(
            const SearchBranch& branch
        ) {
            const auto physical = branch.trace.current_physical;
            switch (branch.trace.phase) {
                case SearchBranchPhase::AtOrigin:
                    if (physical != endpoint_key(branch.trace.origin)) {
                        return phase_invariant_error(branch, "at-origin branch is not located at its origin zone");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "at-origin branch has stop occurrence");
                    }
                    if (branch.trace.parent_branch.has_value()
                        || branch.trace.incoming_segment.has_value()) {
                        return phase_invariant_error(branch, "at-origin branch has predecessor edge");
                    }
                    if (branch.trace.last_timed_segment != nullptr
                        || branch.trace.last_timed_route_segment != nullptr) {
                        return phase_invariant_error(branch, "at-origin branch has timed context");
                    }
                    if (branch.metrics.departure.has_value()
                        || branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "at-origin branch has time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::BeforeFirstBoarding:
                    if (physical.kind != EndpointKind::Stop) {
                        return phase_invariant_error(branch, "preboarding branch is not located at a stop");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "preboarding branch has stop occurrence");
                    }
                    if (!branch.trace.parent_branch.has_value()
                        || !branch.trace.incoming_segment.has_value()) {
                        return phase_invariant_error(branch, "preboarding branch has no access-walk predecessor");
                    }
                    if (branch.trace.last_timed_segment != nullptr
                        || branch.trace.last_timed_route_segment != nullptr) {
                        return phase_invariant_error(branch, "preboarding branch has timed context");
                    }
                    if (branch.metrics.departure.has_value()
                        || branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "preboarding branch has time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::AfterTimedRide:
                    if (physical.kind != EndpointKind::Stop) {
                        return phase_invariant_error(branch, "after-timed branch is not located at a stop");
                    }
                    if (!branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "after-timed branch has no stop occurrence");
                    }
                    if (branch.trace.last_timed_segment == nullptr
                        || branch.trace.last_timed_route_segment == nullptr) {
                        return phase_invariant_error(branch, "after-timed branch has no timed context");
                    }
                    if (!branch.metrics.departure.has_value()
                        || !branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "after-timed branch has incomplete time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::AfterTransferWalk:
                    if (physical.kind != EndpointKind::Stop) {
                        return phase_invariant_error(branch, "after-transfer-walk branch is not located at a stop");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "after-transfer-walk branch has stop occurrence");
                    }
                    if (branch.trace.last_timed_segment == nullptr
                        || branch.trace.last_timed_route_segment == nullptr) {
                        return phase_invariant_error(branch, "after-transfer-walk branch has no timed context");
                    }
                    if (!branch.metrics.departure.has_value()
                        || !branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "after-transfer-walk branch has incomplete time state");
                    }
                    return mathfp::kUnit;

                case SearchBranchPhase::Completed:
                    if (physical.kind != EndpointKind::Zone) {
                        return phase_invariant_error(branch, "completed branch is not located at a zone");
                    }
                    if (branch.trace.current_occurrence.has_value()) {
                        return phase_invariant_error(branch, "completed branch has stop occurrence");
                    }
                    if (branch.trace.last_timed_segment == nullptr
                        || branch.trace.last_timed_route_segment == nullptr) {
                        return phase_invariant_error(branch, "completed branch has no timed context");
                    }
                    if (!branch.metrics.departure.has_value()
                        || !branch.metrics.current_time.has_value()) {
                        return phase_invariant_error(branch, "completed branch has incomplete time state");
                    }
                    return mathfp::kUnit;
            }

            return phase_invariant_error(branch, "unknown branch phase");
        }

        mathfp::Expected<std::optional<SearchBranch>> extend_branch(
              const BranchArena&         branches
            , std::size_t                branch_index
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const ConnectionSegment&   successor
            , const std::optional<WalkExtensionTransition>& walk_transition
            , std::optional<IntervalId>   interval
            , const SearchCostContext&    search_cost
        ) {
            const auto& route_segment = route_segment_at(network, successor.route_segment);
            if (is_walk_connection(successor)) {
                if (!walk_transition.has_value()) {
                    return std::optional<SearchBranch>{};
                }
                const auto next_physical = physical_to_key(route_segment);
                if (branch_revisits_physical(branches, branch, next_physical)) {
                    return std::optional<SearchBranch>{};
                }
                return std::optional<SearchBranch>{
                    extend_with_walk(
                          branch
                        , branch_index
                        , route_segment
                        , successor.id
                        , *walk_transition
                    )
                };
            }

            const auto next_phase = timed_extension_transition(branch.trace.phase);
            if (!next_phase.has_value()) {
                return std::optional<SearchBranch>{};
            }
            const auto next_occurrence = occurrence_key(line_topology_of(route_segment)->to);
            if (branch_revisits_occurrence(branches, branch, next_occurrence)) {
                return std::optional<SearchBranch>{};
            }
            MATHFP_TRY_LET(
                  SearchBranch
                , extended
                , extend_with_timed(
                      branch
                    , branch_index
                    , successor
                    , route_segment
                    , *next_phase
                    , interval
                    , search_cost
                )
            );
            return std::optional<SearchBranch>{ std::move(extended) };
        }

        template <typename Visitor, typename RejectedWalkVisitor>
        void for_each_successor(
              const PreprocessedNetwork& network
            , ZoneId                      origin
            , const ActiveIndexSet&       active_targets
            , std::span<const SearchCompletionTarget> batch_targets
            , const SearchBranch&        branch
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
            , Visitor&&                  visit
            , RejectedWalkVisitor&&      reject_walk
        ) {
            auto&& visitor = visit;
            auto&& walk_rejection_visitor = reject_walk;
            const auto lookup = preprocessing::lookup_from(
                  network.route_index
                , network.connection_index
                , branch.trace.current_physical
            );

            auto visit_walk_connections = [&](std::span<const ConnectionSegmentId> connections) {
                for (const auto connection_id : connections) {
                    const auto& connection = connection_segment_at(network, connection_id);
                    const auto& route_segment = route_segment_at(network, connection.route_segment);
                    if (auto transition = admissible_walk_extension_transition(
                          origin
                        , active_targets
                        , batch_targets
                        , branch
                        , route_segment
                    )) {
                        visitor(connection_id, std::optional<WalkExtensionTransition>{ *transition });
                    }
                }
            };

            switch (branch.trace.phase) {
                case SearchBranchPhase::AtOrigin:
                    visit_walk_connections(lookup.access_walk_connections);
                    break;

                case SearchBranchPhase::AfterTimedRide:
                    visit_walk_connections(lookup.transfer_walk_connections);
                    visit_walk_connections(lookup.egress_walk_connections);
                    break;

                case SearchBranchPhase::BeforeFirstBoarding:
                case SearchBranchPhase::AfterTransferWalk:
                    walk_rejection_visitor(lookup.walk_connections.size());
                    break;

                case SearchBranchPhase::Completed:
                    break;
            }

            for_each_timed_successor(
                  network
                , branch.trace.current_physical
                , branch.metrics.current_time
                , limits
                , first_departure_domain
                , [&](ConnectionSegmentId connection_id) {
                      visitor(connection_id, std::optional<WalkExtensionTransition>{});
                  }
            );
        }

        std::vector<ConnectionSegmentId> connection_segments_of(
            const ConnectionTrace& trace
        ) {
            std::vector<ConnectionSegmentId> segments;
            segments.reserve(trace.legs.size());
            for (const auto& leg : trace.legs) {
                if (leg.connection_segment.has_value()) {
                    segments.push_back(*leg.connection_segment);
                }
            }
            return segments;
        }

        std::vector<ConnectionSegmentId> branch_connection_segments(
              const BranchArena&  branches
            , const SearchBranch& branch
        ) {
            std::vector<ConnectionSegmentId> reversed_segments;
            const auto* cursor = &branch;
            while (cursor->trace.incoming_segment.has_value()) {
                reversed_segments.push_back(*cursor->trace.incoming_segment);
                if (!cursor->trace.parent_branch.has_value()) {
                    break;
                }
                cursor = &branch_at(branches, *cursor->trace.parent_branch);
            }
            std::reverse(reversed_segments.begin(), reversed_segments.end());
            return reversed_segments;
        }

        [[nodiscard]] std::optional<std::size_t> first_timed_segment_position(
              const PreprocessedNetwork&              network
            , std::span<const ConnectionSegmentId>     segments
        ) {
            for (std::size_t i = 0; i < segments.size(); ++i) {
                if (!is_walk_connection(connection_segment_at(network, segments[i]))) {
                    return i;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] Time access_time_before_first_timed(
              const PreprocessedNetwork&              network
            , std::span<const ConnectionSegmentId>     segments
            , std::size_t                             first_timed_pos
        ) {
            Time access_time{ 0.0 };
            for (std::size_t i = 0; i < first_timed_pos; ++i) {
                const auto& segment = connection_segment_at(network, segments[i]);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                access_time = add_time(access_time, route_segment.run_time);
            }
            return access_time;
        }

        mathfp::Expected<ConnectionTrace> materialize_connection_trace(
              const BranchArena&         branches
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
        ) {
            const auto segments = branch_connection_segments(branches, branch);
            const auto first_timed_pos = first_timed_segment_position(network, segments);
            if (!first_timed_pos.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("completed search branch has no timed segment")
                        .ctx("origin", branch.trace.origin.get())
                        .ctx("endpoint_id", branch.trace.current_physical.id)
                );
            }

            const auto& first_timed_segment = connection_segment_at(
                  network
                , segments[*first_timed_pos]
            );
            if (!first_timed_segment.departure.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("first timed segment has no departure while materializing trace")
                        .ctx("segment", first_timed_segment.id.get())
                );
            }

            auto cursor = Time{
                first_timed_segment.departure->value()
                - access_time_before_first_timed(
                      network
                    , segments
                    , *first_timed_pos
                  ).value()
            };
            auto current_physical = endpoint_key(branch.trace.origin);
            bool after_first_timed = false;
            ConnectionTrace trace;
            trace.legs.reserve(segments.size() + branch.metrics.transfers.get());

            for (const auto segment_id : segments) {
                const auto& segment = connection_segment_at(network, segment_id);
                const auto& route_segment = route_segment_at(network, segment.route_segment);
                if (is_walk_connection(segment)) {
                    const auto next_physical = physical_to_key(route_segment);
                    const auto kind = after_first_timed
                        ? (next_physical.kind == EndpointKind::Zone
                            ? ConnectionLegKind::EgressWalk
                            : ConnectionLegKind::TransferWalk)
                        : ConnectionLegKind::AccessWalk;
                    auto leg = make_walk_leg(kind, segment_id, route_segment, cursor);
                    cursor = leg.end_time;
                    current_physical = leg.physical_to;
                    trace.legs.push_back(std::move(leg));
                    continue;
                }

                if (after_first_timed) {
                    if (auto wait_leg = make_transfer_wait_leg(
                          cursor
                        , segment
                        , current_physical
                    )) {
                        cursor = wait_leg->end_time;
                        trace.legs.push_back(*wait_leg);
                    }
                }

                auto ride_leg = make_ride_leg(segment, route_segment);
                cursor = ride_leg.end_time;
                current_physical = ride_leg.physical_to;
                after_first_timed = true;
                trace.legs.push_back(std::move(ride_leg));
            }

            return trace;
        }

        mathfp::Expected<std::optional<SearchConnection>> complete_connection(
              const BranchArena&         branches
            , const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const TransferLimits&      limits
            , ZoneId                     destination
        ) {
            if (!is_complete_connection(branch, destination)) {
                return std::nullopt;
            }

            if (!limits.allow_end_wait
                && branch.trace.last_timed_segment != nullptr
                && branch.trace.incoming_segment.has_value()) {
                const auto& last_segment = connection_segment_at(
                      network
                    , branch.trace.incoming_segment.value()
                );
                if (is_walk_connection(last_segment)) {
                    return std::nullopt;
                }
            }

            MATHFP_TRY_LET(
                  ConnectionTrace
                , trace
                , materialize_connection_trace(branches, branch, network)
            );
            MATHFP_TRY_LET(
                  SearchConnection
                , connection
                , make_search_connection(
                      branch.trace.origin
                    , destination
                    , std::move(trace)
                )
            );
            return std::optional<SearchConnection>{ std::move(connection) };
        }

        [[nodiscard]] bool complete_branch_can_finish(
              const SearchBranch&        branch
            , const PreprocessedNetwork& network
            , const TransferLimits&      limits
            , ZoneId                     destination
        ) {
            if (!is_complete_connection(branch, destination)) {
                return false;
            }

            if (!limits.allow_end_wait
                && branch.trace.last_timed_segment != nullptr
                && branch.trace.incoming_segment.has_value()) {
                const auto& last_segment = connection_segment_at(
                      network
                    , branch.trace.incoming_segment.value()
                );
                if (is_walk_connection(last_segment)) {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] mathfp::Expected<CompleteConnectionMetrics>
        complete_connection_metrics_from_branch(
              const SearchBranch&       branch
            , const SearchCostContext&  search_cost
        ) {
            if (!branch.metrics.departure.has_value()
                || !branch.metrics.current_time.has_value()) {
                return mathfp::unexpected(
                    mathfp::internal_error("completed branch metrics miss departure or arrival")
                        .ctx("origin", branch.trace.origin.get())
                );
            }

            const auto components = SearchCostComponents{
                  .base = partial_impedance_components(branch.metrics)
                , .capacity_exposure = branch.metrics.capacity_exposure
            };
            MATHFP_TRY_LET(
                  double
                , impedance
                , search_impedance(components, search_cost)
            );
            return CompleteConnectionMetrics{
                  .departure   = *branch.metrics.departure
                , .arrival     = *branch.metrics.current_time
                , .journey_time = partial_journey_time(branch.metrics)
                , .transfers    = branch.metrics.transfers
                , .impedance    = impedance
            };
        }

        [[nodiscard]] std::size_t finalize_compact_complete_connection_count(
              const CompactCompleteConnectionRetention& retention
            , const ChoiceTolerances&                   tolerances
            , ChoiceRolloutStage                        rollout_stage
        ) noexcept {
            if (rollout_stage == ChoiceRolloutStage::ExactOnly) {
                return retention.metrics.size();
            }

            const auto summary = summarize_complete_metrics(retention);
            std::size_t count = 0;
            for (const auto& metrics : retention.metrics) {
                if (within_complete_connection_tolerances(
                      metrics
                    , summary
                    , tolerances
                )) {
                    ++count;
                }
            }
            return count;
        }

        mathfp::Expected<CompleteConnectionRetentionDecision>
        retain_exact_compact_complete_connection(
              CompactCompleteConnectionRetention& retention
            , const BranchArena&                  branches
            , const SearchBranch&                 branch
            , CompleteConnectionMetrics           metrics
            , const CompleteConnectionDominanceConfig& dominance_config
        ) {
            for (const auto& known : retention.metrics) {
                if (complete_connection_dominates(
                      dominance_config
                    , known
                    , metrics
                )) {
                    return CompleteConnectionRetentionDecision{
                          .accepted          = false
                        , .removed_dominated = 0
                    };
                }
            }

            auto trace = branch_connection_segments(branches, branch);
            for (const auto& known : retention.traces) {
                if (known == trace) {
                    return CompleteConnectionRetentionDecision{
                          .accepted          = false
                        , .removed_dominated = 0
                    };
                }
            }

            const auto before = retention.metrics.size();
            std::size_t write = 0;
            for (std::size_t read = 0; read < retention.metrics.size(); ++read) {
                if (complete_connection_dominates(
                      dominance_config
                    , metrics
                    , retention.metrics[read]
                )) {
                    continue;
                }
                if (write != read) {
                    retention.metrics[write] = retention.metrics[read];
                    retention.traces[write]  = std::move(retention.traces[read]);
                }
                ++write;
            }
            retention.metrics.resize(write);
            retention.traces.resize(write);
            const auto removed = before - retention.metrics.size();
            retention.metrics.push_back(metrics);
            retention.traces.push_back(std::move(trace));
            return CompleteConnectionRetentionDecision{
                  .accepted          = true
                , .removed_dominated = removed
            };
        }

        mathfp::Expected<SearchPruningDecision> retain_branch(
              const SearchBranch&               branch
            , NodeMetricMap&                    known_metrics
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            if (!branch.metrics.departure.has_value() || !branch.metrics.current_time.has_value()) {
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                };
            }

            ++pruning_stats.evaluated_candidates;
            MATHFP_TRY_LET(
                  PartialPruningMetrics
                , metrics
                , make_partial_pruning_metrics(branch, search_cost)
            );
            const auto node  = search_node_key(branch, pruning_execution);
            auto it          = known_metrics.find(node);
            if (it == known_metrics.end()) {
                if (stores_search_pruning_metrics(pruning_execution)) {
                    insert_pruning_metrics(
                          known_metrics
                        , pruning_execution
                        , node
                        , std::move(metrics)
                    );
                    ++pruning_stats.inserted_metrics;
                } else {
                    ++pruning_stats.skipped_insertions;
                }
                ++pruning_stats.accepted_candidates;
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::Accepted
                    , .accepted = true
                };
            }

            const auto decision = evaluate_search_pruning(
                  pruning_execution
                , metrics
                , it->second
                , params.transfers
            );
            if (!decision.accepted) {
                if (decision.layer == SearchPruningLayer::Exact) {
                    ++pruning_stats.rejected_exact;
                } else {
                    ++pruning_stats.rejected_approximate;
                }
                return decision;
            }

            if (stores_search_pruning_metrics(pruning_execution)) {
                insert_pruning_metrics(
                      known_metrics
                    , pruning_execution
                    , node
                    , std::move(metrics)
                );
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return decision;
        }

        mathfp::Expected<SearchPruningDecision> retain_branch(
              const SearchBranch&               branch
            , SearchProjectionRetention&        retention
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            return retain_branch(
                  branch
                , retention.known_metrics
                , params
                , search_cost
                , pruning_execution
                , pruning_stats
            );
        }

        mathfp::Expected<SearchPruningDecision> retain_branch(
              const SearchBranch&               branch
            , TreePartialRetention&             retention
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            return retain_branch(
                  branch
                , retention.known_metrics
                , params
                , search_cost
                , pruning_execution
                , pruning_stats
            );
        }

        BranchState feasibility_state(
            const SearchBranch& branch
        ) noexcept {
            return BranchState{
                  .current_arrival_time = branch.metrics.current_time
                , .last_segment         = branch.trace.last_timed_segment
                , .last_route_segment   = branch.trace.last_timed_route_segment
                , .transfer_count       = branch.metrics.departure.has_value()
                    ? std::optional<TransferCount>{ branch.metrics.transfers }
                    : std::nullopt
            };
        }

        [[nodiscard]] std::size_t retained_connection_count(
            std::span<const SearchProjectionRetention> retentions
        ) noexcept {
            std::size_t total = 0;
            for (const auto& retention : retentions) {
                total += retention.complete_connections.alternatives.size();
                total += retention.compact_complete_connections.metrics.size();
            }
            return total;
        }

        struct SearchStorageDiagnostics final {
            std::size_t branch_slots{};
            std::size_t live_branches{};
            std::size_t released_branches{};
            std::size_t projection_states{};
            std::size_t tree_pruning_nodes{};
            std::size_t tree_pruning_buckets{};
            float       tree_pruning_load_factor{};
            std::size_t tree_pruning_insertions{};
            std::size_t retained_complete_connections{};
            std::size_t approximate_direct_bytes{};
        };

        [[nodiscard]] SearchStorageDiagnostics search_storage_diagnostics(
              const BranchArena&                         branches
            , std::size_t                                released_branches
            , std::size_t                                projection_state_count
            , std::size_t                                projection_state_size
            , const TreePartialRetention&                tree_retention
            , std::span<const SearchProjectionRetention> retentions
            , const SearchPruningRuntimeStats&           pruning_stats
        ) noexcept {
            const auto pruning_nodes = tree_retention.known_metrics.size();
            const auto pruning_buckets = tree_retention.known_metrics.bucket_count();
            const auto live_branches = branches.size() - released_branches;
            std::size_t compact_complete_metrics = 0;
            for (const auto& retention : retentions) {
                compact_complete_metrics += retention.compact_complete_connections.metrics.size();
            }
            const auto approximate_direct_bytes =
                  branches.size() * sizeof(BranchSlot)
                + live_branches * sizeof(SearchBranch)
                + projection_state_count * projection_state_size
                + pruning_nodes * sizeof(NodeMetricMap::value_type)
                + pruning_buckets * sizeof(void*)
                + compact_complete_metrics * sizeof(CompleteConnectionMetrics);
            return SearchStorageDiagnostics{
                  .branch_slots = branches.size()
                , .live_branches = live_branches
                , .released_branches = released_branches
                , .projection_states = projection_state_count
                , .tree_pruning_nodes = pruning_nodes
                , .tree_pruning_buckets = pruning_buckets
                , .tree_pruning_load_factor = tree_retention.known_metrics.load_factor()
                , .tree_pruning_insertions = pruning_stats.inserted_metrics
                , .retained_complete_connections = retained_connection_count(retentions)
                , .approximate_direct_bytes = approximate_direct_bytes
            };
        }

        [[nodiscard]] std::string format_search_storage_diagnostics(
            const SearchStorageDiagnostics& diagnostics
        ) {
            return fmt::format(
                  "search storage: branch_slots={} live_branches={} released_branches={} projection_states={} tree_pruning(nodes/buckets/load/insertions)={}/{}/{:.3f}/{} retained_complete={} approx_direct_mb={:.2f}"
                , diagnostics.branch_slots
                , diagnostics.live_branches
                , diagnostics.released_branches
                , diagnostics.projection_states
                , diagnostics.tree_pruning_nodes
                , diagnostics.tree_pruning_buckets
                , diagnostics.tree_pruning_load_factor
                , diagnostics.tree_pruning_insertions
                , diagnostics.retained_complete_connections
                , static_cast<double>(diagnostics.approximate_direct_bytes)
                    / (1024.0 * 1024.0)
            );
        }

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

            [[nodiscard]] bool uses_inline_storage() const noexcept {
                return size <= inline_capacity;
            }

            [[nodiscard]] std::size_t word_count() const noexcept {
                return (size + reasons_per_word - 1u) / reasons_per_word;
            }

            [[nodiscard]] std::uint64_t word(std::size_t index) const noexcept {
                return uses_inline_storage()
                    ? inline_words[index]
                    : heap_words[index];
            }

            [[nodiscard]] std::uint64_t& word(std::size_t index) noexcept {
                return uses_inline_storage()
                    ? inline_words[index]
                    : heap_words[index];
            }

            [[nodiscard]] static std::uint64_t reason_code(
                ReachabilityRejectionReason reason
            ) noexcept {
                switch (reason) {
                    case ReachabilityRejectionReason::Phase:
                        return 0u;
                    case ReachabilityRejectionReason::TransferBudget:
                        return 1u;
                    case ReachabilityRejectionReason::UnreachableDestination:
                        return 2u;
                }
                return 2u;
            }

            [[nodiscard]] static ReachabilityRejectionReason reason_from_code(
                std::uint64_t code
            ) noexcept {
                switch (code) {
                    case 0u:
                        return ReachabilityRejectionReason::Phase;
                    case 1u:
                        return ReachabilityRejectionReason::TransferBudget;
                    case 2u:
                    default:
                        return ReachabilityRejectionReason::UnreachableDestination;
                }
            }

            [[nodiscard]] ReachabilityRejectionReason get(
                std::size_t index
            ) const noexcept {
                const auto word_index = index / reasons_per_word;
                const auto bit_offset = (index % reasons_per_word) * bits_per_reason;
                return reason_from_code(
                    (word(word_index) >> bit_offset) & std::uint64_t{ 0b11 }
                );
            }

            void set(
                  std::size_t                 index
                , ReachabilityRejectionReason reason
            ) noexcept {
                if (index >= size) {
                    return;
                }
                const auto word_index = index / reasons_per_word;
                const auto bit_offset = (index % reasons_per_word) * bits_per_reason;
                const auto mask = std::uint64_t{ 0b11 } << bit_offset;
                auto& target_word = word(word_index);
                target_word =
                    (target_word & ~mask)
                    | (reason_code(reason) << bit_offset);
            }
        };

        struct ReachabilityMaskEntry final {
            ActiveIndexSet reachable{};
            CompactReachabilityReasons rejection_reasons{};
        };

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

        [[nodiscard]] ResidualReachabilityKey reachability_mask_key(
              const SearchBranch&   branch
            , const TransferLimits& limits
        ) noexcept {
            return ResidualReachabilityKey{
                  .current_physical    = branch.trace.current_physical
                , .phase               = branch.trace.phase
                , .remaining_transfers = remaining_transfer_budget(branch, limits)
            };
        }

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

            const ReachabilityMaskEntry& target_entry(
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

            const ReachabilityMaskEntry& slot_entry(
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
        };

        [[nodiscard]] ActiveIndexSet filter_target_positions_by_reachability(
              const ActiveIndexSet&                     target_positions
            , const ReachabilityMaskEntry&              reachability_entry
        ) {
            return target_positions.intersect(reachability_entry.reachable);
        }

        [[nodiscard]] ReachabilityRejectionReason summarize_target_reachability_rejection(
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
            target_positions.for_each_difference_index(reachability_entry.reachable, [&](std::size_t target_pos) {
                const auto reason = reachability_entry.rejection_reasons.get(target_pos);
                if (priority(reason) > priority(result)) {
                    result = reason;
                }
            });
            return result;
        }

        [[nodiscard]] ReachabilityTaskFilter filter_task_positions_by_reachability(
              const ActiveIndexSet&        task_positions
            , const ReachabilityMaskEntry& reachability_entry
        ) {
            ReachabilityTaskFilter result{
                  .reachable = task_positions.intersect(reachability_entry.reachable)
            };
            const auto active_count = task_positions.active_count();
            const auto reachable_count = result.reachable.active_count();
            result.unreachable.reserve(active_count - reachable_count);
            task_positions.for_each_difference_index(reachability_entry.reachable, [&](std::size_t task_pos) {
                result.unreachable.push_back(
                    RejectedReachabilityTask{
                          .task_position = task_pos
                        , .reason        = reachability_entry.rejection_reasons.get(task_pos)
                    }
                );
            });
            return result;
        }

        void record_reachability_rejections(
              std::span<const RejectedReachabilityTask> rejected_tasks
            , std::vector<TaskSearchStats>& task_stats
            , TaskSearchStats&              stats
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

        [[nodiscard]] std::vector<std::size_t> matching_complete_tasks(
              const SearchBranch&              branch
            , const ActiveIndexSet&             active_tasks
            , std::span<const SearchProjectionSlot> batch_tasks
        ) {
            std::vector<std::size_t> matches;
            if (!branch.metrics.departure.has_value()
                || branch.trace.current_physical.kind != EndpointKind::Zone) {
                return matches;
            }

            active_tasks.for_each_index([&](std::size_t task_pos) {
                if (batch_tasks[task_pos].destination.get()
                    == branch.trace.current_physical.id) {
                    matches.push_back(task_pos);
                }
            });
            return matches;
        }

        [[nodiscard]] bool matches_completion_target(
              const SearchBranch&                       branch
            , const ActiveIndexSet&                      active_targets
            , std::span<const SearchCompletionTarget>    batch_targets
        ) noexcept {
            if (!branch.metrics.departure.has_value()
                || branch.trace.current_physical.kind != EndpointKind::Zone) {
                return false;
            }

            bool matches = false;
            active_targets.for_each_index([&](std::size_t target_pos) {
                if (batch_targets[target_pos].destination.get()
                    == branch.trace.current_physical.id) {
                    matches = true;
                }
            });
            return matches;
        }

        [[nodiscard]] std::map<ZoneId, std::size_t> completion_target_position_by_destination(
            std::span<const SearchCompletionTarget> batch_targets
        ) {
            std::map<ZoneId, std::size_t> positions;
            for (std::size_t target_pos = 0; target_pos < batch_targets.size(); ++target_pos) {
                positions[batch_targets[target_pos].destination] = target_pos;
            }
            return positions;
        }

        [[nodiscard]] mathfp::Expected<IntervalId> complete_retention_interval(
              const SearchProjectionSlot& slot
            , const SearchCostContext&    search_cost
        ) {
            if (slot.interval.has_value()) {
                return *slot.interval;
            }
            if (search_cost.mode != SearchCostMode::BaseOnly) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("completion-target projection currently requires base search cost")
                        .ctx("origin", slot.origin.get())
                        .ctx("destination", slot.destination.get())
                );
            }
            return IntervalId{ 0 };
        }

        mathfp::Expected<mathfp::Unit> retain_complete_for_slot(
              const SearchBranch&        branch
            , const BranchArena&         branches
            , const PreprocessedNetwork& network
            , const SearchParams&        params
            , const SearchCostContext&   search_cost
            , const SearchProjectionSlot& slot
            , const AssignmentPeriodConfig& assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
            , const CompleteConnectionDominanceConfig& dominance_config
            , SearchProjectionRetention& retention
            , TaskSearchStats&           stats
        ) {
            if (slot.kind == SearchProjectionSlotKind::CompletionTarget) {
                if (!complete_branch_can_finish(
                      branch
                    , network
                    , params.transfers
                    , slot.destination
                )) {
                    return mathfp::kUnit;
                }

                ++stats.completed_connections;
                MATHFP_TRY_LET(
                      CompleteConnectionMetrics
                    , metrics
                    , complete_connection_metrics_from_branch(
                          branch
                        , search_cost
                      )
                );
                const auto retention_decision = retain_exact_compact_complete_connection(
                      retention.compact_complete_connections
                    , branches
                    , branch
                    , metrics
                    , dominance_config
                );
                if (!retention_decision) {
                    return mathfp::unexpected(std::move(retention_decision.error()));
                }
                stats.removed_complete_dominated += retention_decision->removed_dominated;
                if (!retention_decision->accepted) {
                    ++stats.rejected_complete_dominance;
                }
                return mathfp::kUnit;
            }

            MATHFP_TRY_LET(
                  std::optional<SearchConnection>
                , complete
                , complete_connection(
                      branches
                    , branch
                    , network
                    , params.transfers
                    , slot.destination
                  )
            );
            if (complete.has_value()) {
                ++stats.completed_connections;
                if (slot.kind == SearchProjectionSlotKind::DemandTask) {
                    const auto* task = slot.task;
                    if (task == nullptr) {
                        return mathfp::unexpected(
                            mathfp::internal_error("demand projection slot has no task while retaining complete connection")
                        );
                    }
                    const auto connection_metrics = metrics_of(*complete);
                    if (!connection_admissible_for_demand_segment(
                          connection_metrics
                        , task->interval
                        , assignment_period
                        , admissibility_config
                    )) {
                        ++stats.rejected_complete_admissibility;
                        return mathfp::kUnit;
                    }
                }
                MATHFP_TRY_LET(
                      IntervalId
                    , interval
                    , complete_retention_interval(slot, search_cost)
                );
                const auto retention_decision = retain_exact_complete_connection(
                      retention.complete_connections
                    , std::move(*complete)
                    , search_cost
                    , interval
                    , dominance_config
                );
                if (!retention_decision) {
                    return mathfp::unexpected(std::move(retention_decision.error()));
                }
                stats.removed_complete_dominated += retention_decision->removed_dominated;
                if (!retention_decision->accepted) {
                    ++stats.rejected_complete_dominance;
                }
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<std::vector<SearchSlotResult>> search_batch_connections(
              const SearchBatch&                 batch
            , const PreprocessedNetwork&        network
            , const ResidualReverseGraph&        reverse_graph
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const ChoiceConfig&                choice_config
            , const AssignmentPeriodConfig&      assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
            , const SearchPruningExecutionPlan& pruning_execution
            , const CompleteConnectionDominanceConfig& complete_connection_dominance
            , SearchPartialRetentionScope partial_retention_scope
            , SearchDiagnosticsContext          diagnostics
            , std::size_t                       batch_index
            , std::size_t                       batch_count
        ) {
            using timetable::infra::LogLevel;
            using timetable::infra::progress::log;
            using timetable::infra::progress::status;

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

            std::vector<SearchProjectionRetention> retentions;
            retentions.reserve(batch.projection_slots.size());
            for (const auto& slot : batch.projection_slots) {
                retentions.push_back(SearchProjectionRetention{ .slot = slot });
            }
            TreePartialRetention tree_partial_retention{};

            TaskSearchStats                   stats;
            std::vector<TaskSearchStats>      task_stats(batch.projection_slots.size());
            BranchArena                       branches;
            std::deque<std::unique_ptr<FixedActiveMask>> completion_projection_states;
            std::deque<std::unique_ptr<DemandBranchProjectionState>> demand_projection_states;
            std::size_t                       released_branches = 0;
            const SearchTimeDomain*           first_departure_domain = batch.departure_domain;
            const auto                        batch_task_span =
                std::span<const SearchProjectionSlot>{
                      batch.projection_slots.data()
                    , batch.projection_slots.size()
                };
            const auto                        batch_target_span =
                std::span<const SearchCompletionTarget>{
                      batch.completion_targets.data()
                    , batch.completion_targets.size()
                };
            const auto target_projection_slots =
                completion_target_projection_slots(batch_task_span);
            const auto target_positions_by_destination = target_projection_slots
                ? completion_target_position_by_destination(batch_target_span)
                : std::map<ZoneId, std::size_t>{};
            if (target_projection_slots
                && batch.projection_slots.size() > FixedActiveMask::max_size) {
                return mathfp::unexpected(
                    mathfp::internal_error("completion-target fixed active mask capacity exceeded")
                        .ctx("origin", batch.key.origin.get())
                        .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                        .ctx("mask_capacity", static_cast<std::int64_t>(FixedActiveMask::max_size))
                );
            }
            if (target_projection_slots) {
                if (batch.projection_slots.size() != batch.completion_targets.size()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("completion-target projection size disagrees with target count")
                            .ctx("origin", batch.key.origin.get())
                            .ctx("projection_slots", static_cast<std::int64_t>(batch.projection_slots.size()))
                            .ctx("completion_targets", static_cast<std::int64_t>(batch.completion_targets.size()))
                    );
                }
                for (std::size_t target_pos = 0; target_pos < batch.projection_slots.size(); ++target_pos) {
                    const auto& slot = batch.projection_slots[target_pos];
                    const auto& target = batch.completion_targets[target_pos];
                    if (!slot.completion_target.has_value()
                        || slot.completion_target->get() != static_cast<std::int64_t>(target_pos)
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
            }
            const auto                        reachability = build_residual_reachability(
                  reverse_graph
                , batch_target_span
                , params.transfers.max_transfers
                , search_cost.impedance
                , search_cost.fare_scale
            );
            MATHFP_TRY(validate_residual_reachability(
                  reachability
                , params.transfers.max_transfers
            ));
            ReachabilityMaskCache reachability_cache{
                  .reachability   = reachability
                , .targets        = batch_target_span
                , .slots          = batch_task_span
                , .max_transfers  = params.transfers.max_transfers
                , .unified_completion_targets = target_projection_slots
            };

            std::deque<std::size_t> current_frontier;
            std::deque<std::size_t> next_frontier;
            const auto root_branch_index = append_branch(
                branches
              , SearchBranch{
                    .trace = SearchPartialTrace{
                          .origin                   = batch.key.origin
                        , .current_physical         = endpoint_key(batch.key.origin)
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
              }
            );
            if (diagnostics.validate_phase_invariants) {
                MATHFP_TRY(validate_search_branch_phase_invariants(
                    branch_at(branches, root_branch_index)
                ));
            }
            const auto root_reachability_key = reachability_mask_key(
                  branch_at(branches, root_branch_index)
                , params.transfers
            );
            auto root_reachable_targets = filter_target_positions_by_reachability(
                  ActiveIndexSet::full(batch.completion_targets.size())
                , reachability_cache.target_entry(root_reachability_key)
            );
            auto root_reachability = filter_task_positions_by_reachability(
                  ActiveIndexSet::full(batch.projection_slots.size())
                , reachability_cache.slot_entry(root_reachability_key)
            );
            record_reachability_rejections(
                  std::span<const RejectedReachabilityTask>{
                      root_reachability.unreachable.data()
                    , root_reachability.unreachable.size()
                  }
                , task_stats
                , stats
            );
            if (target_projection_slots) {
                if (!root_reachability.reachable.equals(root_reachable_targets)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("completion-target root reachability masks disagree")
                            .ctx("origin", batch.key.origin.get())
                    );
                }
                completion_projection_states.push_back(
                    std::make_unique<FixedActiveMask>(
                        FixedActiveMask::from(root_reachability.reachable)
                    )
                );
            } else {
                demand_projection_states.push_back(
                    std::make_unique<DemandBranchProjectionState>(
                        DemandBranchProjectionState{
                              .active_tasks = std::move(root_reachability.reachable)
                            , .active_targets = std::move(root_reachable_targets)
                        }
                    )
                );
            }
            current_frontier.push_back(root_branch_index);
            BranchPhaseStats current_frontier_by_phase;
            BranchPhaseStats next_frontier_by_phase;
            increment_phase_stats(
                  current_frontier_by_phase
                , SearchBranchPhase::AtOrigin
            );

            status(
                fmt::format(
                      "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} frontier={} found={}"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                    , batch.projection_slots.size()
                    , batch.completion_targets.size()
                    , diagnostics.capacity_iteration
                    , current_frontier.size()
                    , retained_connection_count(retentions)
                )
            );

            using Clock = std::chrono::steady_clock;
            const auto batch_started_at = Clock::now();
            auto last_wall_clock_heartbeat = batch_started_at;
            auto projection_state_count = [&]() noexcept {
                return target_projection_slots
                    ? completion_projection_states.size() - released_branches
                    : demand_projection_states.size() - released_branches;
            };
            const auto projection_state_size = target_projection_slots
                ? sizeof(FixedActiveMask)
                : sizeof(DemandBranchProjectionState);
            auto emit_storage_diagnostics = [&]() {
                log(
                    format_search_storage_diagnostics(
                        search_storage_diagnostics(
                              branches
                            , released_branches
                            , projection_state_count()
                            , projection_state_size
                            , tree_partial_retention
                            , std::span<const SearchProjectionRetention>{
                                  retentions.data()
                                , retentions.size()
                              }
                            , stats.pruning
                        )
                    )
                    , LogLevel::Info
                );
            };
            auto emit_wall_clock_heartbeat =
                [&](const char* stage, std::size_t branch_index) {
                    const auto now = Clock::now();
                    if (now - last_wall_clock_heartbeat
                        < kSearchWallClockHeartbeatInterval) {
                        return;
                    }
                    last_wall_clock_heartbeat = now;
                    const auto elapsed_ms = std::chrono::duration_cast<
                        std::chrono::milliseconds
                    >(now - batch_started_at).count();
                    log(
                        fmt::format(
                              "search wall heartbeat: batch={:>8} origin={:>4} interval={:>4}"
                              " tasks={:>5} targets={:>5} capacity_iteration={:>4} stage={} branch={} elapsed_ms={}"
                              " expanded={:>8} generated={:>8} accepted={:>8} found={:>8}"
                              " frontier={}/{}"
                              " frontier_phase(current={}, next={})"
                              " walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                              " accepted_phase({})"
                            , static_cast<std::int64_t>(batch_index)
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , batch.projection_slots.size()
                            , batch.completion_targets.size()
                            , diagnostics.capacity_iteration
                            , stage
                            , static_cast<std::int64_t>(branch_index)
                            , static_cast<std::int64_t>(elapsed_ms)
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , retained_connection_count(retentions)
                            , current_frontier.size()
                            , next_frontier.size()
                            , format_branch_phase_stats(current_frontier_by_phase)
                            , format_branch_phase_stats(next_frontier_by_phase)
                            , format_walk_kind_stats(stats.generated_walk)
                            , format_walk_kind_stats(stats.accepted_walk)
                            , stats.rejected_consecutive_walk
                            , format_branch_phase_stats(stats.accepted_branches_by_phase)
                        )
                        , LogLevel::Info
                    );
                    emit_storage_diagnostics();
                };
            auto release_projection_payload = [&](std::size_t released_index) noexcept {
                if (target_projection_slots) {
                    if (released_index < completion_projection_states.size()) {
                        completion_projection_states[released_index].reset();
                    }
                } else {
                    if (released_index < demand_projection_states.size()) {
                        demand_projection_states[released_index].reset();
                    }
                }
                ++released_branches;
            };

            while (!current_frontier.empty() || !next_frontier.empty()) {
                if (current_frontier.empty()) {
                    current_frontier.swap(next_frontier);
                    current_frontier_by_phase = next_frontier_by_phase;
                    next_frontier_by_phase = BranchPhaseStats{};
                }

                stats.max_current_frontier = std::max(stats.max_current_frontier, current_frontier.size());
                stats.max_next_frontier    = std::max(stats.max_next_frontier   , next_frontier   .size());

                const auto branch_index = current_frontier.front();
                current_frontier.pop_front();
                const auto& branch = branch_at(branches, branch_index);
                decrement_phase_stats(current_frontier_by_phase, branch.trace.phase);
                emit_wall_clock_heartbeat("branch", branch_index);
                ActiveIndexSet completion_active;
                const ActiveIndexSet* active_tasks_ptr{};
                const ActiveIndexSet* active_targets_ptr{};
                if (target_projection_slots) {
                    completion_active =
                        completion_projection_states[branch_index]->to_active_index_set();
                    active_tasks_ptr   = &completion_active;
                    active_targets_ptr = &completion_active;
                } else {
                    const auto& projection_state = *demand_projection_states[branch_index];
                    active_tasks_ptr   = &projection_state.active_tasks;
                    active_targets_ptr = &projection_state.active_targets;
                }
                const auto& active_tasks   = *active_tasks_ptr;
                const auto& active_targets = *active_targets_ptr;
                ++stats.expanded_branches;

                if ((stats.expanded_branches % kSearchHeartbeatStep) == 0) {
                    status(
                        fmt::format(
                              "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} expanded={} accepted={} found={} frontier={}/{}"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , batch.projection_slots.size()
                            , batch.completion_targets.size()
                            , diagnostics.capacity_iteration
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_connection_count(retentions)
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                    );
                    log(
                        fmt::format(
                              "search heartbeat: batch={:>8} origin={:>4} interval={:>4} tasks={:>5} targets={:>5} capacity_iteration={:>4} expanded={:>8} generated={:>8}"
                              " accepted={:>8} found={:>8} rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                              " lower_bound_pruned={}"
                              " pruning(exact/approx/inserted/skipped)={}/{}/{}/{}"
                              " frontier={}/{}"
                              " frontier_phase(current={}, next={})"
                              " walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                              " accepted_phase({})"
                            , static_cast<std::int64_t>(batch_index)
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , batch.projection_slots.size()
                            , batch.completion_targets.size()
                            , diagnostics.capacity_iteration
                            , stats.expanded_branches
                            , stats.generated_successors
                            , stats.accepted_branches
                            , retained_connection_count(retentions)
                            , stats.rejected_time_domain
                            , stats.rejected_feasibility
                            , stats.rejected_reboarding
                            , stats.rejected_cycles
                            , stats.rejected_transfer_limit
                            , stats.rejected_reachability
                            , stats.rejected_dominance_or_tolerance
                            , stats.rejected_suffix_lower_bound
                            , stats.pruning.rejected_exact
                            , stats.pruning.rejected_approximate
                            , stats.pruning.inserted_metrics
                            , stats.pruning.skipped_insertions
                            , current_frontier.size()
                            , next_frontier   .size()
                            , format_branch_phase_stats(current_frontier_by_phase)
                            , format_branch_phase_stats(next_frontier_by_phase)
                            , format_walk_kind_stats(stats.generated_walk)
                            , format_walk_kind_stats(stats.accepted_walk)
                            , stats.rejected_consecutive_walk
                            , format_branch_phase_stats(stats.accepted_branches_by_phase)
                        )
                        , LogLevel::Info
                    );
                    log(
                        format_search_pruning_runtime_stats(summarize(stats.pruning))
                        , LogLevel::Info
                    );
                    emit_storage_diagnostics();
                }

                if (active_targets.empty()
                    || (branch.trace.current_physical.kind == EndpointKind::Zone
                        && branch.metrics.departure.has_value())) {
                    release_branch_if_closed(
                          branches
                        , branch_index
                        , release_projection_payload
                    );
                    continue;
                }

                mathfp::Expected<mathfp::Unit> successor_error = mathfp::kUnit;
                for_each_successor(
                      network
                    , batch.key.origin
                    , active_targets
                    , batch_target_span
                    , branch
                    , params.transfers
                    , first_departure_domain
                    , [&](ConnectionSegmentId successor_id, std::optional<WalkExtensionTransition> walk_transition) {
                    if (!successor_error) {
                        return;
                    }
                    ++stats.generated_successors;
                    if (walk_transition.has_value()) {
                        increment_walk_kind_stats(stats.generated_walk, walk_transition->kind);
                    }
                    if ((stats.generated_successors % kSearchWallClockSuccessorCheckStep) == 0) {
                        emit_wall_clock_heartbeat("successor", branch_index);
                    }
                    const auto& successor               = connection_segment_at(network, successor_id);
                    const auto& successor_route_segment = route_segment_at(
                          network
                        , successor.route_segment
                    );
                    if (!first_timed_departure_allowed(
                          branch
                        , successor
                        , first_departure_domain
                        , params.transfers
                    )) {
                        ++stats.rejected_time_domain;
                        return;
                    }
                    if (!is_branch_extension_feasible(
                          feasibility_state(branch)
                        , successor
                        , successor_route_segment
                        , params.transfers
                    )) {
                        ++stats.rejected_feasibility;
                        return;
                    }
                    if (!improves_repeated_stop_reboarding(
                          branch
                        , network
                        , successor
                        , successor_route_segment
                    )) {
                        ++stats.rejected_reboarding;
                        return;
                    }

                    auto candidate_result = extend_branch(
                          branches
                        , branch_index
                        , branch
                        , network
                        , successor
                        , walk_transition
                        , batch.key.interval
                        , search_cost
                    );
                    if (!candidate_result) {
                        successor_error = mathfp::unexpected(
                            std::move(candidate_result.error())
                        );
                        return;
                    }
                    auto candidate = std::move(*candidate_result);
                    if (!candidate.has_value()) {
                        ++stats.rejected_cycles;
                        return;
                    }
                    if (diagnostics.validate_phase_invariants) {
                        if (auto invariant_result = validate_search_branch_phase_invariants(*candidate);
                            !invariant_result) {
                            successor_error = mathfp::unexpected(
                                std::move(invariant_result.error())
                            );
                            return;
                        }
                    }

                    if (candidate->metrics.transfers > params.transfers.max_transfers) {
                        ++stats.rejected_transfer_limit;
                        return;
                    }

                    std::vector<std::size_t> complete_task_positions;
                    bool completed_target = false;
                    if (target_projection_slots
                        && candidate->metrics.departure.has_value()
                        && candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        const auto position_it = target_positions_by_destination.find(
                            ZoneId{ candidate->trace.current_physical.id }
                        );
                        if (position_it != target_positions_by_destination.end()
                            && active_targets.contains(position_it->second)) {
                            completed_target = true;
                            complete_task_positions.push_back(position_it->second);
                        }
                    } else {
                        completed_target = matches_completion_target(
                              *candidate
                            , active_targets
                            , batch_target_span
                        );
                        if (completed_target) {
                            complete_task_positions = matching_complete_tasks(
                                  *candidate
                                , active_tasks
                                , batch_task_span
                            );
                        }
                    }
                    if (completed_target) {
                        bool retained_complete = false;
                        for (const auto task_pos : complete_task_positions) {
                            const auto completed_before = task_stats[task_pos].completed_connections;
                            auto complete_result = retain_complete_for_slot(
                                  *candidate
                                , branches
                                , network
                                , params
                                , search_cost
                                , batch.projection_slots[task_pos]
                                , assignment_period
                                , admissibility_config
                                , complete_connection_dominance
                                , retentions[task_pos]
                                , task_stats[task_pos]
                            );
                            if (!complete_result) {
                                successor_error = mathfp::unexpected(
                                    std::move(complete_result.error())
                                );
                                return;
                            }
                            stats.completed_connections +=
                                task_stats[task_pos].completed_connections - completed_before;
                            retained_complete =
                                retained_complete
                                || task_stats[task_pos].completed_connections > completed_before;
                        }
                        if (retained_complete && walk_transition.has_value()) {
                            increment_walk_kind_stats(stats.accepted_walk, walk_transition->kind);
                        }
                        return;
                    }

                    if (candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }

                    const auto candidate_reachability_key = reachability_mask_key(
                          *candidate
                        , params.transfers
                    );
                    const auto& target_reachability_entry =
                        reachability_cache.target_entry(candidate_reachability_key);
                    auto next_active_targets = filter_target_positions_by_reachability(
                          active_targets
                        , target_reachability_entry
                    );
                    if (next_active_targets.empty()) {
                        add_reachability_rejection(
                              stats
                            , summarize_target_reachability_rejection(
                                  active_targets
                                , target_reachability_entry
                              )
                        );
                        return;
                    }

                    if (partial_retention_scope
                        == SearchPartialRetentionScope::TreeGlobal) {
                        auto pruning_decision = retain_branch(
                              *candidate
                            , tree_partial_retention
                            , params
                            , search_cost
                            , pruning_execution
                            , stats.pruning
                        );
                        if (!pruning_decision) {
                            successor_error = mathfp::unexpected(
                                std::move(pruning_decision.error())
                            );
                            return;
                        }
                        if (!pruning_decision->accepted) {
                            ++stats.rejected_dominance_or_tolerance;
                            return;
                        }
                    }

                    const auto reachable_tasks = filter_task_positions_by_reachability(
                          active_tasks
                        , reachability_cache.slot_entry(candidate_reachability_key)
                    );
                    record_reachability_rejections(
                          std::span<const RejectedReachabilityTask>{
                              reachable_tasks.unreachable.data()
                            , reachable_tasks.unreachable.size()
                          }
                        , task_stats
                        , stats
                    );

                    ActiveIndexSet next_active_tasks{ batch.projection_slots.size() };
                    std::vector<RejectedSuffixLowerBoundTask> lower_bound_rejected_tasks;
                    lower_bound_rejected_tasks.reserve(reachable_tasks.reachable.active_count());
                    reachable_tasks.reachable.for_each_index([&](std::size_t task_pos) {
                        if (!successor_error) {
                            return;
                        }
                        auto lower_bound_decision = target_projection_slots
                            ? evaluate_suffix_lower_bound_pruning(
                                  *candidate
                                , batch.projection_slots[task_pos].destination
                                , reachability
                                , retentions[task_pos].compact_complete_connections
                                , params
                                , search_cost
                                , choice_config
                                , complete_connection_dominance
                              )
                            : evaluate_suffix_lower_bound_pruning(
                                  *candidate
                                , batch.projection_slots[task_pos].destination
                                , reachability
                                , retentions[task_pos].complete_connections
                                , params
                                , search_cost
                                , choice_config
                                , complete_connection_dominance
                              );
                        if (!lower_bound_decision) {
                            successor_error = mathfp::unexpected(
                                std::move(lower_bound_decision.error())
                            );
                            return;
                        }
                        if (!lower_bound_decision->feasible) {
                            lower_bound_rejected_tasks.push_back(
                                RejectedSuffixLowerBoundTask{
                                      .task_position = task_pos
                                    , .reason        = lower_bound_decision->rejection_reason
                                }
                            );
                            ++task_stats[task_pos].rejected_dominance_or_tolerance;
                            return;
                        }

                        if (partial_retention_scope
                            == SearchPartialRetentionScope::ProjectionSlotLocal) {
                            auto pruning_decision = retain_branch(
                                  *candidate
                                , retentions[task_pos]
                                , params
                                , search_cost
                                , pruning_execution
                                , stats.pruning
                            );
                            if (!pruning_decision) {
                                successor_error = mathfp::unexpected(
                                    std::move(pruning_decision.error())
                                );
                                return;
                            }
                            if (!pruning_decision->accepted) {
                                ++task_stats[task_pos].rejected_dominance_or_tolerance;
                                return;
                            }
                        }
                        next_active_tasks.set(task_pos);
                    });
                    if (!successor_error) {
                        return;
                    }
                    record_suffix_lower_bound_rejections(
                          std::span<const RejectedSuffixLowerBoundTask>{
                              lower_bound_rejected_tasks.data()
                            , lower_bound_rejected_tasks.size()
                          }
                        , task_stats
                        , stats
                    );
                    if (next_active_tasks.empty()) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }

                    ++stats.accepted_branches;
                    if (walk_transition.has_value()) {
                        increment_walk_kind_stats(stats.accepted_walk, walk_transition->kind);
                    }
                    increment_phase_stats(
                          stats.accepted_branches_by_phase
                        , candidate->trace.phase
                    );
                    const auto same_level =
                        is_walk_connection(successor) || !branch.metrics.departure.has_value();
                    const auto candidate_phase = candidate->trace.phase;
                    const auto candidate_index = append_branch(
                          branches
                        , std::move(*candidate)
                    );
                    if (target_projection_slots) {
                        completion_projection_states.push_back(
                            std::make_unique<FixedActiveMask>(
                                FixedActiveMask::from(next_active_tasks)
                            )
                        );
                    } else {
                        demand_projection_states.push_back(
                            std::make_unique<DemandBranchProjectionState>(
                                DemandBranchProjectionState{
                                      .active_tasks = std::move(next_active_tasks)
                                    , .active_targets = std::move(next_active_targets)
                                }
                            )
                        );
                    }
                    if (same_level) {
                        current_frontier.push_back(candidate_index);
                        increment_phase_stats(current_frontier_by_phase, candidate_phase);
                    } else {
                        next_frontier   .push_back(candidate_index);
                        increment_phase_stats(next_frontier_by_phase, candidate_phase);
                    }
                }
                    , [&](std::size_t rejected_walk_count) {
                          stats.rejected_consecutive_walk += rejected_walk_count;
                      }
                );
                release_branch_if_closed(
                      branches
                    , branch_index
                    , release_projection_payload
                );
                MATHFP_TRY(std::move(successor_error));
            }

            std::vector<SearchSlotResult> slot_results;
            slot_results.reserve(batch.projection_slots.size());
            std::size_t batch_final_found = 0;
            std::size_t batch_retained_before_tolerance = 0;
            for (std::size_t task_pos = 0; task_pos < batch.projection_slots.size(); ++task_pos) {
                const auto& slot = batch.projection_slots[task_pos];
                auto& retention   = retentions[task_pos];
                const auto before_tolerance = target_projection_slots
                    ? retention.compact_complete_connections.metrics.size()
                    : retention.complete_connections.alternatives.size();
                std::vector<SearchConnection> connections;
                const auto connection_count = target_projection_slots
                    ? finalize_compact_complete_connection_count(
                          retention.compact_complete_connections
                        , params.choice_tolerances
                        , choice_config.rollout_stage
                      )
                    : [&]() {
                          connections = finalize_complete_connection_retention(
                                retention.complete_connections
                              , params.choice_tolerances
                              , choice_config.rollout_stage
                          );
                          return connections.size();
                      }();
                task_stats[task_pos].rejected_complete_tolerance =
                    before_tolerance - connection_count;
                stats.rejected_complete_admissibility += task_stats[task_pos].rejected_complete_admissibility;
                stats.rejected_complete_dominance += task_stats[task_pos].rejected_complete_dominance;
                stats.removed_complete_dominated  += task_stats[task_pos].removed_complete_dominated;
                stats.rejected_complete_tolerance += task_stats[task_pos].rejected_complete_tolerance;
                batch_final_found += connection_count;
                batch_retained_before_tolerance += before_tolerance;
                slot_results.push_back(
                    SearchSlotResult{
                          .slot = slot
                        , .connection_count = connection_count
                        , .connections = std::move(connections)
                    }
                );
                MATHFP_TRY(validate_reachability_rejection_stats(task_stats[task_pos]));
                MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(task_stats[task_pos]));

                log(
                    fmt::format(
                          "search batch projection done: batch={}/{} slot={} kind={} task={} origin={} destination={} interval={} found={:>8}"
                          " retained_complete={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                          " reachability_pruned={} reachability_detail(phase/budget/unreachable)={}/{}/{}"
                          " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                        , batch_index + 1
                        , batch_count
                        , static_cast<std::int64_t>(task_pos)
                        , slot.kind == SearchProjectionSlotKind::DemandTask
                            ? "demand_task"
                            : "completion_target"
                        , slot.task_ref.has_value()
                            ? std::to_string(slot.task_ref->get())
                            : std::string{"<none>"}
                        , slot.origin.get()
                        , slot.destination.get()
                        , slot.interval.has_value()
                            ? std::to_string(slot.interval->get())
                            : std::string{"<none>"}
                        , slot_results.back().connection_count
                        , before_tolerance
                        , task_stats[task_pos].rejected_complete_admissibility
                        , task_stats[task_pos].rejected_complete_dominance
                        , task_stats[task_pos].rejected_complete_tolerance
                        , task_stats[task_pos].removed_complete_dominated
                        , task_stats[task_pos].rejected_reachability
                        , task_stats[task_pos].reachability_rejections.phase
                        , task_stats[task_pos].reachability_rejections.transfer_budget
                        , task_stats[task_pos].reachability_rejections.unreachable_destination
                        , task_stats[task_pos].rejected_suffix_lower_bound
                        , task_stats[task_pos].suffix_lower_bound_rejections.exact_dominance
                        , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_impedance
                        , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_journey_time
                        , task_stats[task_pos].suffix_lower_bound_rejections.tolerance_transfers
                    )
                    , LogLevel::Info
                );
            }
            MATHFP_TRY(validate_reachability_rejection_stats(stats));
            MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(stats));

            log(
                fmt::format(
                      "search batch done: {}/{} origin={} interval={} tasks={} found={:>8} completed={:>8}"
                      " retained_complete={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                      " expanded={:>8} generated={:>8} accepted={:>8}"
                      " rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                      " reachability_detail(phase/budget/unreachable)={}/{}/{} max_frontier={}/{}"
                      " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                      " walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
                      " accepted_phase({})"
                    , batch_index + 1
                    , batch_count
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                    , batch.projection_slots.size()
                    , batch_final_found
                    , stats.completed_connections
                    , batch_retained_before_tolerance
                    , stats.rejected_complete_admissibility
                    , stats.rejected_complete_dominance
                    , stats.rejected_complete_tolerance
                    , stats.removed_complete_dominated
                    , stats.expanded_branches
                    , stats.generated_successors
                    , stats.accepted_branches
                    , stats.rejected_time_domain
                    , stats.rejected_feasibility
                    , stats.rejected_reboarding
                    , stats.rejected_cycles
                    , stats.rejected_transfer_limit
                    , stats.rejected_reachability
                    , stats.rejected_dominance_or_tolerance
                    , stats.reachability_rejections.phase
                    , stats.reachability_rejections.transfer_budget
                    , stats.reachability_rejections.unreachable_destination
                    , stats.max_current_frontier
                    , stats.max_next_frontier
                    , stats.rejected_suffix_lower_bound
                    , stats.suffix_lower_bound_rejections.exact_dominance
                    , stats.suffix_lower_bound_rejections.tolerance_impedance
                    , stats.suffix_lower_bound_rejections.tolerance_journey_time
                    , stats.suffix_lower_bound_rejections.tolerance_transfers
                    , format_walk_kind_stats(stats.generated_walk)
                    , format_walk_kind_stats(stats.accepted_walk)
                    , stats.rejected_consecutive_walk
                    , format_branch_phase_stats(stats.accepted_branches_by_phase)
                )
                , LogLevel::Info
            );
            log(
                format_search_pruning_runtime_stats(summarize(stats.pruning))
                , LogLevel::Info
            );
            emit_storage_diagnostics();

            return slot_results;
        }

    }  // namespace

    SearchConnection::SearchConnection(
        Connection connection
    )
        : connection_(std::move(connection)) {}

    mathfp::Expected<std::vector<SearchTask>> build_search_tasks(
        const InputModel& input
    ) {
        auto intervals_result = interval_lookup(input);
        if (!intervals_result) {
            return mathfp::unexpected(std::move(intervals_result.error()));
        }
        auto intervals = std::move(*intervals_result);
        const auto task_departure_padding = SearchTimePadding{};
        std::vector<SearchTask> tasks;
        tasks.reserve(input.demand.size());

        for (const auto& demand : input.demand) {
            if (!(demand.passengers > 0.0)) {
                continue;
            }

            const auto interval_it = intervals.find(demand.interval);
            if (interval_it == intervals.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("search task demand entry references unknown interval")
                        .ctx("origin"     , demand.origin     .get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval   .get())
                );
            }

            MATHFP_TRY_LET(
                  SearchTimeDomain
                , departure_domain
                , make_search_time_domain(std::vector<SearchTimeWindow>{
                    expand_interval_to_search_window(
                          *interval_it->second
                        , task_departure_padding
                    )
                })
            );

            tasks.push_back(
                SearchTask{
                      .index            = SearchTaskRef{ static_cast<std::int64_t>(tasks.size()) }
                    , .origin           = demand.origin
                    , .destination      = demand.destination
                    , .interval         = *interval_it->second
                    , .departure_domain = std::move(departure_domain)
                }
            );
        }

        return tasks;
    }

    std::vector<SearchCompletionTarget> make_search_completion_targets(
        const std::map<ZoneId, bool>& destinations
    ) {
        std::vector<SearchCompletionTarget> targets;
        targets.reserve(destinations.size());
        for (const auto& [destination, _] : destinations) {
            targets.push_back(
                SearchCompletionTarget{
                      .index = SearchCompletionTargetRef{
                          static_cast<std::int64_t>(targets.size())
                      }
                    , .destination = destination
                }
            );
        }
        return targets;
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask> tasks
        , const SearchTimeDomain&     period_domain
    ) {
        MATHFP_TRY(validate_search_time_domain(period_domain));

        struct MutableOriginJob final {
            std::vector<SearchTaskRef> projection_tasks{};
            std::map<ZoneId, bool>     completion_destinations{};
        };

        std::map<ZoneId, MutableOriginJob> grouped;
        for (const auto& task : tasks) {
            auto& job = grouped[task.origin];
            job.projection_tasks.push_back(task.index);
            job.completion_destinations.emplace(task.destination, true);
        }

        std::vector<SearchTreeJob> jobs;
        jobs.reserve(grouped.size());
        for (auto& [origin, job] : grouped) {
            jobs.push_back(
                SearchTreeJob{
                      .index = SearchTreeJobRef{ static_cast<std::int64_t>(jobs.size()) }
                    , .origin = origin
                    , .departure_domain = period_domain
                    , .completion_targets = make_search_completion_targets(
                          job.completion_destinations
                      )
                    , .projection_tasks = std::move(job.projection_tasks)
                }
            );
        }

        return jobs;
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
        , SearchDestinationScope            destination_scope
        , std::span<const Zone>             declared_zones
    ) {
        MATHFP_TRY(validate_search_time_domain_execution(execution));
        std::map<ZoneId, mathfp::Unit> declared_destination_ids;
        for (const auto& zone : declared_zones) {
            const auto [_, inserted] = declared_destination_ids.emplace(
                  zone.id
                , mathfp::kUnit
            );
            if (!inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search contains duplicate declared destination zone")
                        .ctx("zone", zone.id.get())
                );
            }
        }

        struct MutableOriginJob final {
            std::vector<SearchTaskRef> projection_tasks{};
            std::map<ZoneId, bool>     completion_destinations{};
        };

        std::map<ZoneId, MutableOriginJob> grouped;
        for (const auto& task : tasks) {
            auto& job = grouped[task.origin];
            if (destination_scope == SearchDestinationScope::DeclaredZones
                && !declared_destination_ids.contains(task.destination)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search task references destination outside declared destination scope")
                        .ctx("destination", task.destination.get())
                        .ctx("task", task.index.get())
                );
            }
            job.projection_tasks.push_back(task.index);
            job.completion_destinations.emplace(task.destination, true);
        }

        switch (destination_scope) {
            case SearchDestinationScope::DemandDestinations:
                break;

            case SearchDestinationScope::DeclaredZones:
                if (declared_destination_ids.empty()) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("origin-period search with declared destination scope requires declared zones")
                    );
                }
                for (auto& entry : grouped) {
                    auto& job = entry.second;
                    for (const auto& [destination, _] : declared_destination_ids) {
                        job.completion_destinations.emplace(destination, true);
                    }
                }
                break;
        }

        std::vector<SearchTreeJob> jobs;
        jobs.reserve(grouped.size());
        for (auto& [origin, job] : grouped) {
            const auto* domain = find_origin_search_time_domain(execution, origin);
            if (domain == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search has no executable search-time domain for active origin")
                        .ctx("origin", origin.get())
                        .ctx("source_mode", std::string(to_string(execution.source_mode)))
                        .ctx("adaptation", std::string(to_string(execution.adaptation)))
                );
            }
            jobs.push_back(
                SearchTreeJob{
                      .index = SearchTreeJobRef{ static_cast<std::int64_t>(jobs.size()) }
                    , .origin = origin
                    , .departure_domain = *domain
                    , .completion_targets = make_search_completion_targets(
                          job.completion_destinations
                      )
                    , .projection_tasks = std::move(job.projection_tasks)
                }
            );
        }

        return jobs;
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
    ) {
        return build_origin_period_search_tree_jobs(
              tasks
            , execution
            , SearchDestinationScope::DemandDestinations
            , std::span<const Zone>{}
        );
    }

    mathfp::Expected<std::vector<SearchTreeJob>> build_declared_origin_period_search_tree_jobs(
          std::span<const Zone>             declared_zones
        , std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
        , SearchDestinationScope            destination_scope
    ) {
        MATHFP_TRY(validate_search_time_domain_execution(execution));

        struct MutableOriginJob final {
            std::vector<SearchTaskRef> projection_tasks{};
            std::map<ZoneId, bool>     completion_destinations{};
        };

        std::map<ZoneId, MutableOriginJob> grouped;
        for (const auto& zone : declared_zones) {
            const auto [_, inserted] = grouped.emplace(zone.id, MutableOriginJob{});
            if (!inserted) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search contains duplicate zone")
                        .ctx("zone", zone.id.get())
                );
            }
        }

        switch (destination_scope) {
            case SearchDestinationScope::DemandDestinations:
                break;

            case SearchDestinationScope::DeclaredZones:
                for (auto& entry : grouped) {
                    auto& job = entry.second;
                    for (const auto& zone : declared_zones) {
                        job.completion_destinations.emplace(zone.id, true);
                    }
                }
                break;
        }

        for (const auto& task : tasks) {
            auto it = grouped.find(task.origin);
            if (it == grouped.end()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search task references origin outside declared zones")
                        .ctx("origin", task.origin.get())
                        .ctx("task", task.index.get())
                    );
            }
            if (destination_scope == SearchDestinationScope::DeclaredZones
                && !grouped.contains(task.destination)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search task references destination outside declared zones")
                        .ctx("destination", task.destination.get())
                        .ctx("task", task.index.get())
                );
            }
            auto& job = it->second;
            job.projection_tasks.push_back(task.index);
            job.completion_destinations.emplace(task.destination, true);
        }

        std::vector<SearchTreeJob> jobs;
        jobs.reserve(grouped.size());
        for (auto& [origin, job] : grouped) {
            const auto* domain = find_origin_search_time_domain(execution, origin);
            if (domain == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("declared origin-period search has no executable search-time domain for origin")
                        .ctx("origin", origin.get())
                        .ctx("source_mode", std::string(to_string(execution.source_mode)))
                        .ctx("adaptation", std::string(to_string(execution.adaptation)))
                );
            }

            jobs.push_back(
                SearchTreeJob{
                      .index = SearchTreeJobRef{ static_cast<std::int64_t>(jobs.size()) }
                    , .origin = origin
                    , .departure_domain = *domain
                    , .completion_targets = make_search_completion_targets(
                          job.completion_destinations
                      )
                    , .projection_tasks = std::move(job.projection_tasks)
                }
            );
        }

        return jobs;
    }

    namespace {

        [[nodiscard]] SearchProjectionSlot make_demand_task_projection_slot(
              const SearchTask& task
            , std::size_t       result_index
        ) noexcept {
            return SearchProjectionSlot{
                  .kind         = SearchProjectionSlotKind::DemandTask
                , .origin       = task.origin
                , .destination  = task.destination
                , .interval     = task.interval.id
                , .task_ref     = task.index
                , .task         = &task
                , .result_index = result_index
            };
        }

        mathfp::Expected<std::vector<SearchProjectionSlot>> build_demand_projection_slots(
            std::span<const SearchTask> tasks
        ) {
            std::vector<SearchProjectionSlot> slots;
            slots.reserve(tasks.size());
            std::map<SearchTaskRef, mathfp::Unit> seen;
            for (std::size_t i = 0; i < tasks.size(); ++i) {
                const auto [_, inserted] = seen.emplace(tasks[i].index, mathfp::kUnit);
                if (!inserted) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("duplicate search task ref while building demand projection slots")
                            .ctx("task", tasks[i].index.get())
                    );
                }
                slots.push_back(make_demand_task_projection_slot(tasks[i], i));
            }
            return slots;
        }

        [[nodiscard]] std::vector<SearchProjectionSlot> build_completion_target_projection_slots(
            const SearchTreeJob& job
        ) {
            std::vector<SearchProjectionSlot> slots;
            slots.reserve(job.completion_targets.size());
            for (const auto& target : job.completion_targets) {
                slots.push_back(
                    SearchProjectionSlot{
                          .kind              = SearchProjectionSlotKind::CompletionTarget
                        , .origin            = job.origin
                        , .destination       = target.destination
                        , .interval          = std::nullopt
                        , .task              = nullptr
                        , .result_index      = std::nullopt
                        , .completion_target = target.index
                    }
                );
            }
            return slots;
        }

        mathfp::Expected<std::vector<SearchBatch>> build_origin_period_search_batches(
              std::span<const SearchTask>      tasks
            , std::span<const SearchTreeJob>   tree_jobs
            , SearchResultProjection           result_projection
        ) {
            std::map<SearchTaskRef, SearchProjectionSlot> slot_by_task;
            if (result_projection == SearchResultProjection::DemandTasks) {
                MATHFP_TRY_LET(
                      std::vector<SearchProjectionSlot>
                    , demand_slots
                    , build_demand_projection_slots(tasks)
                );
                for (const auto& slot : demand_slots) {
                    slot_by_task.emplace(*slot.task_ref, slot);
                }
            }

            std::vector<SearchBatch> batches;
            batches.reserve(tree_jobs.size());
            for (const auto& job : tree_jobs) {
                SearchBatch batch{
                      .key = SearchBatchKey{
                            .origin = job.origin
                          , .interval = std::nullopt
                          , .departure_windows = job.departure_domain.windows
                      }
                    , .departure_domain = &job.departure_domain
                    , .completion_targets = {}
                    , .projection_slots = {}
                };
                batch.completion_targets = job.completion_targets;
                switch (result_projection) {
                    case SearchResultProjection::DemandTasks:
                        batch.projection_slots.reserve(job.projection_tasks.size());
                        for (const auto task_ref : job.projection_tasks) {
                            const auto it = slot_by_task.find(task_ref);
                            if (it == slot_by_task.end()) {
                                return mathfp::unexpected(
                                    mathfp::internal_error("origin-period search tree job references unknown search task")
                                        .ctx("job", job.index.get())
                                        .ctx("origin", job.origin.get())
                                        .ctx("task", task_ref.get())
                                );
                            }
                            batch.projection_slots.push_back(it->second);
                        }
                        break;

                    case SearchResultProjection::CompletionTargets:
                        batch.projection_slots =
                            build_completion_target_projection_slots(job);
                        break;
                    }
                batches.push_back(std::move(batch));
            }

            return batches;
        }

        std::vector<SearchBatch> build_interval_local_search_batches(
            std::span<const SearchTask> tasks
        ) {
            std::vector<SearchBatch> batches;
            std::map<SearchBatchKey, std::size_t> index_by_key;

            for (std::size_t i = 0; i < tasks.size(); ++i) {
                const auto& task = tasks[i];
                SearchBatchKey key{
                      .origin            = task.origin
                    , .interval          = task.interval.id
                    , .departure_windows = task.departure_domain.windows
                };

                const auto [it, inserted] = index_by_key.emplace(key, batches.size());
                if (inserted) {
                    batches.push_back(
                        SearchBatch{
                              .key              = std::move(key)
                            , .departure_domain = &task.departure_domain
                            , .completion_targets = {
                                  SearchCompletionTarget{
                                        .index = SearchCompletionTargetRef{ 0 }
                                      , .destination = task.destination
                                  }
                              }
                            , .projection_slots = {}
                        }
                    );
                } else {
                    auto& targets = batches[it->second].completion_targets;
                    const auto exists = std::any_of(
                          targets.begin()
                        , targets.end()
                        , [&](const SearchCompletionTarget& target) {
                            return target.destination == task.destination;
                        }
                    );
                    if (!exists) {
                        targets.push_back(
                            SearchCompletionTarget{
                                  .index = SearchCompletionTargetRef{
                                      static_cast<std::int64_t>(targets.size())
                                  }
                                , .destination = task.destination
                            }
                        );
                    }
                }

                batches[it->second].projection_slots.push_back(
                    make_demand_task_projection_slot(task, i)
                );
            }

            return batches;
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
              SearchOriginScope          origin_scope
            , std::size_t                declared_zone_count
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
            if (config.mode == SearchExecutionMode::IntervalLocal
                && config.result_projection != SearchResultProjection::DemandTasks) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("interval-local search supports only demand-task projection")
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                );
            }
            if (config.result_projection == SearchResultProjection::CompletionTargets
                && config.mode != SearchExecutionMode::OriginPeriod) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("completion-target projection requires origin-period search")
                        .ctx("execution_mode", std::string(to_string(config.mode)))
                );
            }
            if (config.partial_retention_scope == SearchPartialRetentionScope::TreeGlobal
                && config.result_projection != SearchResultProjection::CompletionTargets) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("tree-global partial retention currently requires completion-target projection")
                        .ctx("result_projection", std::string(to_string(config.result_projection)))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<SearchTimeDomainSummary> summarize_batch_search_domains(
            std::span<const SearchBatch> batches
        ) {
            std::vector<SearchTimeWindow> windows;
            for (const auto& batch : batches) {
                if (batch.departure_domain == nullptr) {
                    continue;
                }
                windows.insert(
                      windows.end()
                    , batch.departure_domain->windows.begin()
                    , batch.departure_domain->windows.end()
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

        struct SearchBatchExecutionDiagnostics final {
            std::size_t completion_target_count{};
            std::size_t projection_task_count{};
            std::size_t zero_completion_target_tree_count{};
            std::size_t zero_projection_task_tree_count{};
            std::size_t max_completion_targets_per_tree{};
            std::size_t max_projection_tasks_per_tree{};
        };

        [[nodiscard]] SearchBatchExecutionDiagnostics summarize_search_batches(
            std::span<const SearchBatch> batches
        ) noexcept {
            SearchBatchExecutionDiagnostics summary{};
            for (const auto& batch : batches) {
                summary.completion_target_count += batch.completion_targets.size();
                summary.projection_task_count   += batch.projection_slots.size();
                if (batch.completion_targets.empty()) {
                    ++summary.zero_completion_target_tree_count;
                }
                if (batch.projection_slots.empty()) {
                    ++summary.zero_projection_task_tree_count;
                }
                summary.max_completion_targets_per_tree = std::max(
                      summary.max_completion_targets_per_tree
                    , batch.completion_targets.size()
                );
                summary.max_projection_tasks_per_tree = std::max(
                      summary.max_projection_tasks_per_tree
                    , batch.projection_slots.size()
                );
            }
            return summary;
        }

        mathfp::Expected<mathfp::Unit> validate_search_batch_projection_contract(
              std::span<const SearchBatch> batches
            , SearchExecutionMode          execution_mode
            , SearchResultProjection       result_projection
            , std::span<const SearchTask>  tasks
        ) {
            for (std::size_t batch_pos = 0; batch_pos < batches.size(); ++batch_pos) {
                const auto& batch = batches[batch_pos];
                if (batch.departure_domain == nullptr) {
                    return mathfp::unexpected(
                        mathfp::internal_error("search batch has no departure domain")
                            .ctx("batch", static_cast<std::int64_t>(batch_pos))
                            .ctx("origin", batch.key.origin.get())
                    );
                }

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
                                || slot.task == nullptr
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
                            const auto& task = *slot.task;
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

                        case SearchResultProjection::CompletionTargets:
                            if (slot.kind != SearchProjectionSlotKind::CompletionTarget
                                || !slot.completion_target.has_value()
                                || slot.task != nullptr
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

        [[nodiscard]] std::size_t search_slot_connection_count(
            std::span<const SearchSlotResult> results
        ) noexcept {
            std::size_t total = 0;
            for (const auto& result : results) {
                total += result.connection_count;
            }
            return total;
        }

        [[nodiscard]] std::size_t all_zone_search_worker_count(
            std::size_t batch_count
        ) noexcept {
            if (batch_count == 0u) {
                return 0u;
            }
            const auto hardware = std::max(
                  1u
                , std::thread::hardware_concurrency()
            );
            return std::min(
                  batch_count
                , std::min<std::size_t>(
                      static_cast<std::size_t>(hardware)
                    , kAllZoneMaxParallelBatches
                  )
            );
        }

        [[nodiscard]] ConnectionSearchResult materialize_demand_task_search_result(
              std::span<const SearchTask>       tasks
            , std::vector<SearchSlotResult>     slot_results
        ) {
            ConnectionSearchResult result;
            result.task_results.reserve(tasks.size());
            for (const auto& task : tasks) {
                result.task_results.push_back(
                    SearchTaskResult{
                          .task        = task
                        , .connections = {}
                    }
                );
            }

            for (auto& slot_result : slot_results) {
                if (slot_result.slot.kind != SearchProjectionSlotKind::DemandTask
                    || !slot_result.slot.result_index.has_value()) {
                    continue;
                }
                result.task_results[*slot_result.slot.result_index].connections =
                    std::move(slot_result.connections);
            }
            return result;
        }

        struct CountOnlyAllZoneSearchResultSink final {
            std::mutex mutex{};
            std::map<ZoneId, std::map<ZoneId, std::size_t>> counts{};

            mathfp::Expected<mathfp::Unit> accept(
                std::vector<SearchSlotResult> slot_results
            ) {
                std::lock_guard lock{ mutex };
                for (auto& slot_result : slot_results) {
                    if (slot_result.slot.kind != SearchProjectionSlotKind::CompletionTarget) {
                        continue;
                    }
                    counts[slot_result.slot.origin][slot_result.slot.destination] =
                        slot_result.connection_count;
                }
                return mathfp::kUnit;
            }

            [[nodiscard]] AllZoneConnectionSearchResult materialize() {
                std::lock_guard lock{ mutex };
                AllZoneConnectionSearchResult result;
                result.tree_results.reserve(counts.size());
                for (const auto& [origin, targets] : counts) {
                    AllZoneTreeResult tree{
                          .origin = origin
                        , .target_results = {}
                    };
                    tree.target_results.reserve(targets.size());
                    for (const auto& [destination, connection_count] : targets) {
                        tree.target_results.push_back(
                            AllZoneTargetResult{
                                  .origin           = origin
                                , .destination      = destination
                                , .connection_count = connection_count
                                , .connections      = {}
                            }
                        );
                    }
                    result.tree_results.push_back(std::move(tree));
                }
                return result;
            }
        };

    }  // namespace

    std::size_t all_zone_target_connection_count(
        const AllZoneTargetResult& target
    ) noexcept {
        if (target.connection_count == 0u && !target.connections.empty()) {
            return target.connections.size();
        }
        return target.connection_count;
    }

    std::vector<const SearchConnection*> search_connection_ptrs(
        const ConnectionSearchResult& result
    ) {
        std::vector<const SearchConnection*> connections;
        const auto total = search_connection_count(result);
        connections.reserve(total);
        for (const auto& task_result : result.task_results) {
            for (const auto& connection : task_result.connections) {
                connections.push_back(&connection);
            }
        }
        return connections;
    }

    std::size_t search_connection_count(
        const ConnectionSearchResult& result
    ) noexcept {
        std::size_t total = 0;
        for (const auto& task_result : result.task_results) {
            total += task_result.connections.size();
        }
        return total;
    }

    std::size_t search_connection_count(
        const AllZoneConnectionSearchResult& result
    ) noexcept {
        std::size_t total = 0;
        for (const auto& tree_result : result.tree_results) {
            for (const auto& target_result : tree_result.target_results) {
                total += all_zone_target_connection_count(target_result);
            }
        }
        return total;
    }

    mathfp::Expected<SearchConnection> make_search_connection(
        Connection connection
    ) {
        return make_search_connection(
              connection.origin
            , connection.destination
            , std::move(connection.trace)
        );
    }

    mathfp::Expected<SearchConnection> make_search_connection(
          ZoneId          origin
        , ZoneId          destination
        , ConnectionTrace trace
    ) {
        MATHFP_TRY_LET(
              Connection
            , connection
            , make_connection(origin, destination, std::move(trace))
        );
        return SearchConnection{ std::move(connection) };
    }

    const Connection& canonical_connection(
        const SearchConnection& connection
    ) noexcept {
        return connection.connection_;
    }

    ZoneId origin_of(
        const SearchConnection& connection
    ) noexcept {
        return canonical_connection(connection).origin;
    }

    ZoneId destination_of(
        const SearchConnection& connection
    ) noexcept {
        return canonical_connection(connection).destination;
    }

    ConnectionMetrics metrics_of(
        const SearchConnection& connection
    ) {
        return *compute_connection_metrics(canonical_connection(connection));
    }

    Time departure_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).departure_time;
    }

    Time arrival_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).arrival_time;
    }

    Time journey_time_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).journey_time;
    }

    Time transfer_time_of(
        const SearchConnection& connection
    ) {
        const auto metrics = metrics_of(connection);
        return metrics.transfer_wait_time + metrics.transfer_walk_time;
    }

    TransferCount transfer_count_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).transfer_count;
    }

    double fare_of(
        const SearchConnection& connection
    ) {
        return metrics_of(connection).fare;
    }

    std::vector<ConnectionSegmentId> connection_segment_trace(
        const SearchConnection& connection
    ) {
        return connection_segments_of(canonical_connection(connection).trace);
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        const auto execution_mode = execution.config.mode;
        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::DemandTasks) {
            return mathfp::unexpected(
                mathfp::invalid_arg("ConnectionSearchResult search currently supports only DemandTasks result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));

        both("search: branch-and-bound");
        log(
            fmt::format(
                "search input: route_segments = {:>8}  connection_segments = {:>8}"
                "  tasks = {:>8}  execution_mode = {}  max_transfers = {}"
                , network.route_segments     .size()
                , network.connection_segments.size()
                , tasks.size()
                , to_string(execution_mode)
                , params.transfers.max_transfers.get()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search cost: mode={}  fare_scale={:.6f}  capacity_iteration={}  capacity_index(loads/capacities/trips/prefixes)={}/{}/{}/{}"
                , to_string(search_cost.mode)
                , search_cost.fare_scale
                , diagnostics.capacity_iteration
                , search_cost.capacity.index.load_positions.size()
                , search_cost.capacity.index.capacity_positions.size()
                , search_cost.capacity.index.capacity_trip_positions.size()
                , search_cost.capacity.index.penalty_prefixes.size()
            )
            , LogLevel::Info
        );

        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , default_pruning_execution
            , plan_search_pruning_execution(
                  SearchPruningModelConfig{
                      .requested_state_space =
                          SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  }
                , SearchPruningRolloutStage::Disabled
                , params.search_tolerances
            )
        );
        const auto& effective_pruning_execution =
            pruning_execution != nullptr
                ? *pruning_execution
                : default_pruning_execution;

        log(
            fmt::format(
                "search setup: tasks = {:>8}  fare_scale = {:.6f}  execution_mode = {}"
                , tasks.size()
                , search_cost.fare_scale
                , to_string(execution_mode)
            )
            , LogLevel::Info
        );
        log(
            format_search_pruning_execution_summary(summarize(effective_pruning_execution))
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search projection contract: result_projection={} partial_retention_scope={} complete_retention=projection_slot_local"
                , to_string(execution.config.result_projection)
                , to_string(execution.config.partial_retention_scope)
            )
            , LogLevel::Info
        );

        if (tasks.empty()) {
            status("search: no positive-demand search tasks available");
        }

        std::vector<SearchTreeJob> origin_period_tree_jobs;
        std::vector<SearchBatch>   batches;
        if (execution_mode == SearchExecutionMode::OriginPeriod) {
            if (search_cost.mode != SearchCostMode::BaseOnly) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search currently supports only base search cost")
                        .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
                );
            }
            if (execution.time_domain_execution == nullptr) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search requires SearchTimeDomainExecution")
                );
            }
            mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
                execution.config.origin_scope == SearchOriginScope::DeclaredZones
                    ? build_declared_origin_period_search_tree_jobs(
                          execution.declared_zones
                        , tasks
                        , *execution.time_domain_execution
                        , execution.config.destination_scope
                      )
                    : build_origin_period_search_tree_jobs(
                          tasks
                        , *execution.time_domain_execution
                        , execution.config.destination_scope
                        , execution.declared_zones
                      );
            if (!tree_jobs_result) {
                return mathfp::unexpected(std::move(tree_jobs_result.error()));
            }
            auto tree_jobs = std::move(*tree_jobs_result);
            origin_period_tree_jobs = std::move(tree_jobs);
            MATHFP_TRY_LET(
                  std::vector<SearchBatch>
                , origin_batches
                , build_origin_period_search_batches(
                      tasks
                    , origin_period_tree_jobs
                    , execution.config.result_projection
                )
            );
            batches = std::move(origin_batches);
            log(
                fmt::format(
                    "search tree jobs: mode={} jobs = {:>8}  tasks = {:>8}"
                    , to_string(execution_mode)
                    , origin_period_tree_jobs.size()
                    , tasks.size()
                )
                , LogLevel::Info
            );
        } else {
            batches = build_interval_local_search_batches(tasks);
        }
        MATHFP_TRY(validate_search_batch_projection_contract(
              batches
            , execution_mode
            , execution.config.result_projection
            , tasks
        ));

        const auto residual_reverse_graph = build_residual_reverse_graph(
              network.route_segments
            , network.connection_segments
        );
        const auto batch_execution_diagnostics = summarize_search_batches(batches);
        const auto expected_tree_count = expected_search_tree_count(
              execution.config.origin_scope
            , diagnostics.declared_zone_count
            , tasks
        );
        MATHFP_TRY_LET(
              SearchTimeDomainSummary
            , search_domain_summary
            , summarize_batch_search_domains(batches)
        );
        log(
            fmt::format(
                "search batching: mode={} batches = {:>8}  tasks = {:>8}"
                , to_string(execution_mode)
                , batches.size()
                , tasks.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "search diagnostics: search_execution_mode={} origin_scope={} time_domain_source={} destination_scope={} result_projection={} partial_retention_scope={} phase_invariant_validation={}"
                  " tree_count={} expected_tree_count={} tree_count_delta={} search_origin_count={} declared_zone_count={} declared_zone_request_count={}"
                  " active_demand_origin_count={} completion_target_count={} projection_slot_count={}"
                  " zero_completion_target_tree_count={} zero_projection_task_tree_count={}"
                  " max_completion_targets_per_tree={} max_projection_tasks_per_tree={}"
                  " search_domain_summary=\"{}\""
                , to_string(execution_mode)
                , to_string(execution.config.origin_scope)
                , to_string(execution.config.time_domain_source)
                , to_string(execution.config.destination_scope)
                , to_string(execution.config.result_projection)
                , to_string(execution.config.partial_retention_scope)
                , diagnostics.validate_phase_invariants ? "on" : "off"
                , batches.size()
                , expected_tree_count
                , signed_count_delta(batches.size(), expected_tree_count)
                , search_origin_count(batches)
                , diagnostics.declared_zone_count
                , execution.declared_zones.size()
                , active_demand_origin_count(tasks)
                , batch_execution_diagnostics.completion_target_count
                , batch_execution_diagnostics.projection_task_count
                , batch_execution_diagnostics.zero_completion_target_tree_count
                , batch_execution_diagnostics.zero_projection_task_tree_count
                , batch_execution_diagnostics.max_completion_targets_per_tree
                , batch_execution_diagnostics.max_projection_tasks_per_tree
                , format_search_time_domain_summary(search_domain_summary)
            )
            , LogLevel::Info
        );

        std::vector<SearchSlotResult> slot_results;
        for (std::size_t i = 0; i < batches.size(); ++i) {
            const auto& batch = batches[i];
            const auto total_found = search_slot_connection_count(slot_results);
            if (i == 0 || (i % kTaskProgressStep) == 0 || (i + 1) == batches.size()) {
                status(
                    fmt::format(
                          "search: batch {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} total_found={}"
                        , i + 1
                        , batches.size()
                        , batch.key.origin.get()
                        , format_batch_interval(batch.key.interval)
                        , batch.projection_slots.size()
                        , batch.completion_targets.size()
                        , diagnostics.capacity_iteration
                        , total_found
                    )
                );
            }
            log(
                fmt::format(
                      "search batch start: {}/{} origin={} interval={} tasks={} targets={} capacity_iteration={} cumulative_found={}"
                    , i + 1
                    , batches.size()
                    , batch.key.origin.get()
                    , format_batch_interval(batch.key.interval)
                    , batch.projection_slots.size()
                    , batch.completion_targets.size()
                    , diagnostics.capacity_iteration
                    , total_found
                )
                , LogLevel::Info
            );
            MATHFP_TRY_LET(
                  std::vector<SearchSlotResult>
                , batch_slot_results
                , search_batch_connections(
                      batch
                    , network
                    , residual_reverse_graph
                    , params
                    , search_cost
                    , choice_config
                    , assignment_period
                    , admissibility_config
                    , effective_pruning_execution
                    , complete_connection_dominance
                    , execution.config.partial_retention_scope
                    , diagnostics
                    , i
                    , batches.size()
                )
            );
            slot_results.insert(
                  slot_results.end()
                , std::make_move_iterator(batch_slot_results.begin())
                , std::make_move_iterator(batch_slot_results.end())
            );
        }

        auto result = materialize_demand_task_search_result(
              tasks
            , std::move(slot_results)
        );
        log(
            fmt::format(
                  "search result: tasks = {:>8}  connections = {:>8}"
                , result.task_results.size()
                , search_connection_count(result)
            )
            , LogLevel::Info
        );
        both("search: branch-and-bound done");
        return result;
    }

    mathfp::Expected<AllZoneConnectionSearchResult> search_all_zone_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionRequest     execution
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::CompletionTargets) {
            return mathfp::unexpected(
                mathfp::invalid_arg("AllZoneConnectionSearchResult search requires CompletionTargets result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        if (execution.config.mode != SearchExecutionMode::OriginPeriod) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone search requires origin-period execution")
                    .ctx("execution_mode", std::string(to_string(execution.config.mode)))
            );
        }
        if (search_cost.mode != SearchCostMode::BaseOnly) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone completion-target search currently supports only base search cost")
                    .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
            );
        }
        if (execution.time_domain_execution == nullptr) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone origin-period search requires SearchTimeDomainExecution")
            );
        }

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));

        both("search: all-zone branch-and-bound");
        log(
            fmt::format(
                  "all-zone projection contract: partial_retention_scope={} complete_retention=completion_target_slot"
                , to_string(execution.config.partial_retention_scope)
            )
            , LogLevel::Info
        );
        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , default_pruning_execution
            , plan_search_pruning_execution(
                  SearchPruningModelConfig{
                      .requested_state_space =
                          SearchPruningStateSpace::CurrentPhysicalOccurrenceAndTransferContext
                  }
                , SearchPruningRolloutStage::Disabled
                , params.search_tolerances
            )
        );
        const auto& effective_pruning_execution =
            pruning_execution != nullptr
                ? *pruning_execution
                : default_pruning_execution;

        mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
            execution.config.origin_scope == SearchOriginScope::DeclaredZones
                ? build_declared_origin_period_search_tree_jobs(
                      execution.declared_zones
                    , tasks
                    , *execution.time_domain_execution
                    , execution.config.destination_scope
                  )
                : build_origin_period_search_tree_jobs(
                      tasks
                    , *execution.time_domain_execution
                    , execution.config.destination_scope
                    , execution.declared_zones
                  );
        if (!tree_jobs_result) {
            return mathfp::unexpected(std::move(tree_jobs_result.error()));
        }
        auto tree_jobs = std::move(*tree_jobs_result);
        MATHFP_TRY_LET(
              std::vector<SearchBatch>
            , batches
            , build_origin_period_search_batches(
                  tasks
                , tree_jobs
                , execution.config.result_projection
            )
        );
        MATHFP_TRY(validate_search_batch_projection_contract(
              batches
            , execution.config.mode
            , execution.config.result_projection
            , tasks
        ));

        const auto residual_reverse_graph = build_residual_reverse_graph(
              network.route_segments
            , network.connection_segments
        );
        const auto batch_execution_diagnostics = summarize_search_batches(batches);
        const auto expected_tree_count = expected_search_tree_count(
              execution.config.origin_scope
            , diagnostics.declared_zone_count
            , tasks
        );
        log(
            fmt::format(
                  "all-zone search comparison diagnostics: trees={} expected_trees={} tree_count_delta={} batches={} targets={} projection_slots={} partial_retention_scope={} phase_invariant_validation={} result_sink=count_only"
                , tree_jobs.size()
                , expected_tree_count
                , signed_count_delta(tree_jobs.size(), expected_tree_count)
                , batches.size()
                , batch_execution_diagnostics.completion_target_count
                , batch_execution_diagnostics.projection_task_count
                , to_string(execution.config.partial_retention_scope)
                , diagnostics.validate_phase_invariants ? "on" : "off"
            )
            , LogLevel::Info
        );

        const auto worker_count = all_zone_search_worker_count(batches.size());
        log(
            fmt::format(
                  "all-zone search parallel execution: workers={} batches={} max_parallel_batches={}"
                , worker_count
                , batches.size()
                , kAllZoneMaxParallelBatches
            )
            , LogLevel::Info
        );

        std::atomic<std::size_t> next_batch{ 0u };
        std::atomic<std::size_t> completed_batches{ 0u };
        std::atomic<std::size_t> found_connections{ 0u };
        CountOnlyAllZoneSearchResultSink result_sink;
        std::vector<std::future<mathfp::Expected<mathfp::Unit>>> workers;
        workers.reserve(worker_count);

        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.push_back(
                std::async(
                      std::launch::async
                    , [&, worker]() -> mathfp::Expected<mathfp::Unit> {
                          for (;;) {
                              const auto i = next_batch.fetch_add(
                                    1u
                                  , std::memory_order_relaxed
                              );
                              if (i >= batches.size()) {
                                  return mathfp::kUnit;
                              }

                              const auto& batch = batches[i];
                              if (
                                     i == 0
                                  || (i % kTaskProgressStep) == 0
                                  || (i + 1) == batches.size()
                              ) {
                                  status(
                                      fmt::format(
                                            "all-zone search: worker={} batch {}/{} origin={} targets={} projection_slots={} completed={} total_found={}"
                                          , worker
                                          , i + 1
                                          , batches.size()
                                          , batch.key.origin.get()
                                          , batch.completion_targets.size()
                                          , batch.projection_slots.size()
                                          , completed_batches.load(std::memory_order_relaxed)
                                          , found_connections.load(std::memory_order_relaxed)
                                      )
                                  );
                              }

                              MATHFP_TRY_LET(
                                    std::vector<SearchSlotResult>
                                  , results
                                  , search_batch_connections(
                                        batch
                                      , network
                                      , residual_reverse_graph
                                      , params
                                      , search_cost
                                      , choice_config
                                      , assignment_period
                                      , admissibility_config
                                      , effective_pruning_execution
                                      , complete_connection_dominance
                                      , execution.config.partial_retention_scope
                                      , diagnostics
                                      , i
                                      , batches.size()
                                  )
                              );
                              found_connections.fetch_add(
                                    search_slot_connection_count(results)
                                  , std::memory_order_relaxed
                              );
                              MATHFP_TRY(result_sink.accept(std::move(results)));
                              completed_batches.fetch_add(1u, std::memory_order_relaxed);
                          }
                      }
                )
            );
        }

        for (auto& worker : workers) {
            MATHFP_TRY(worker.get());
        }

        auto result = result_sink.materialize();
        log(
            fmt::format(
                  "all-zone search result: trees = {:>8}  expected_trees = {:>8}  connections = {:>8}"
                , result.tree_results.size()
                , expected_tree_count
                , search_connection_count(result)
            )
            , LogLevel::Info
        );
        both("search: all-zone branch-and-bound done");
        return result;
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , SearchExecutionRequest{
                  .config = make_interval_local_search_execution_config()
              }
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionMode        execution_mode
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , SearchExecutionRequest{
                  .config = make_search_execution_config(execution_mode)
              }
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , SearchExecutionMode        execution_mode
        , const SearchParams&        params
        , const SearchCostContext&   search_cost
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , SearchExecutionRequest{
                  .config = make_search_execution_config(execution_mode)
              }
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , double                     fare_scale
        , const SearchParams&        params
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics
    ) {
        MATHFP_TRY_LET(
              SearchCostContext
            , search_cost
            , make_base_search_cost_context(params.impedance, fare_scale)
        );
        return search_connections_branch_and_bound(
              network
            , tasks
            , params
            , search_cost
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , complete_connection_dominance
            , diagnostics
        );
    }

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork& network
        , std::span<const SearchTask> tasks
        , double                     fare_scale
        , const SearchParams&        params
        , const ChoiceConfig&         choice_config
        , const AssignmentPeriodConfig& assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , SearchDiagnosticsContext diagnostics
    ) {
        return search_connections_branch_and_bound(
              network
            , tasks
            , fare_scale
            , params
            , choice_config
            , assignment_period
            , admissibility_config
            , pruning_execution
            , CompleteConnectionDominanceConfig{}
            , diagnostics
        );
    }

}  // namespace timetable::domain::assignment
