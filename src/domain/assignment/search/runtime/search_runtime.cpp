#include "timetable/domain/assignment/search/runtime/search_runtime.hpp"

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

#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/od_day_path_contract.hpp"
#include "timetable/domain/assignment/od_day_path_runtime.hpp"
#include "timetable/domain/assignment/search_cost.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_diagnostics.hpp"
#include "timetable/domain/assignment/search_time_domain_diagnostics.hpp"
#include "timetable/domain/assignment/validation.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/assignment/search/frontier/active_index_set.hpp"
#include "timetable/domain/assignment/search/frontier/branch_arena.hpp"
#include "timetable/domain/assignment/search/frontier/retention_operations.hpp"
#include "timetable/domain/assignment/search/generation/branch_transition.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/execution.hpp"
#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/assignment/search/projection/contract.hpp"
#include "timetable/domain/assignment/search/projection/complete_connection.hpp"
#include "timetable/domain/assignment/search/pruning/suffix_lower_bound.hpp"
#include "timetable/domain/assignment/search/model/retention.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/od_day/successor.hpp"
#include "timetable/domain/assignment/search/od_day/supply_graph.hpp"
#include "timetable/domain/assignment/search/relations/branch_metrics.hpp"
#include "timetable/domain/assignment/search/relations/branch_state_projection.hpp"
#include "timetable/domain/assignment/search/relations/paper_connection_relevance.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search/runtime/diagnostics.hpp"
#include "timetable/domain/segment_semantics.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment::runtime {
    namespace {

        using PartialPruningMetrics = SearchPruningMetrics;

        struct SearchCancellationToken final {
            std::atomic_bool requested{ false };
        };

        [[nodiscard]] bool search_cancelled(
            const SearchCancellationToken* token
        ) noexcept {
            return token != nullptr
                && token->requested.load(std::memory_order_acquire);
        }

        void request_search_cancellation(
            SearchCancellationToken* token
        ) noexcept {
            if (token != nullptr) {
                token->requested.store(true, std::memory_order_release);
            }
        }

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

        enum class OdDaySearchComputationContract : std::uint8_t {
              IncrementalDayPathRetention
            , StructuralEdgeExpansion
            , PaperConnectionSegmentTree
        };

        [[nodiscard]] constexpr const char* to_log_token(
            OdDaySearchComputationContract contract
        ) noexcept {
            switch (contract) {
                case OdDaySearchComputationContract::IncrementalDayPathRetention:
                    return "incremental_day_path_retention";
                case OdDaySearchComputationContract::StructuralEdgeExpansion:
                    return "structural_edge_expansion";
                case OdDaySearchComputationContract::PaperConnectionSegmentTree:
                    return "paper_connection_segment_tree";
            }
            return "unknown";
        }

        [[nodiscard]] std::string format_batch_interval(
            const std::optional<IntervalId>& interval
        ) {
            if (!interval.has_value()) {
                return "period";
            }
            return std::to_string(interval->get());
        }

        struct RejectedReachabilityTask final {
            std::size_t                 task_position{};
            ReachabilityRejectionReason reason{ ReachabilityRejectionReason::UnreachableDestination };
        };

        struct RejectedSuffixLowerBoundTask final {
            std::size_t                     task_position{};
            SuffixLowerBoundRejectionReason reason{ SuffixLowerBoundRejectionReason::ToleranceImpedance };
        };

        constexpr std::size_t kTaskProgressStep    = 10;
        constexpr std::size_t kSearchHeartbeatStep = 100'000;
        constexpr std::size_t kSearchWallClockSuccessorCheckStep = 16'384;
        constexpr std::size_t kInitialTaskBranchReserve = 4'096;
        constexpr auto kSearchWallClockHeartbeatInterval =
            std::chrono::seconds{ 30 };

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
              PaperConnectionNodeMetricMap&     known_metrics
            , const SearchPruningExecutionPlan& pruning_execution
            , PaperConnectionNodeKey            node
            , PartialPruningMetrics             metrics
            , PaperConnectionLabelId            label
            , std::vector<PaperConnectionLabelId>& removed_labels
        ) {
            auto& known = known_metrics[node];
            insert_paper_node_connection_metrics(
                  pruning_execution
                , known
                , std::move(metrics)
                , label
                , removed_labels
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

        [[nodiscard]] DayPathLeg make_day_path_walk_leg(
              ConnectionLegKind   kind
            , const RouteSegment& route_segment
        ) noexcept {
            return DayPathLeg{
                  .kind            = kind
                , .route_segment   = route_segment.id
                , .physical_from   = physical_from_key(route_segment)
                , .physical_to     = physical_to_key(route_segment)
                , .occurrence_from = std::nullopt
                , .occurrence_to   = std::nullopt
                , .line            = std::nullopt
                , .route           = std::nullopt
            };
        }

        [[nodiscard]] DayPathLeg make_day_path_ride_leg(
            const RouteSegment& route_segment
        ) noexcept {
            const auto* line = line_topology_of(route_segment);
            return DayPathLeg{
                  .kind            = ConnectionLegKind::Ride
                , .route_segment   = route_segment.id
                , .physical_from   = physical_from_key(route_segment)
                , .physical_to     = physical_to_key(route_segment)
                , .occurrence_from = occurrence_key(line->from)
                , .occurrence_to   = occurrence_key(line->to)
                , .line            = line->line
                , .route           = line->route
            };
        }

        void add_paper_successor_generation_diagnostics(
              TaskSearchStats&                          stats
            , const PaperSuccessorGenerationDiagnostics& diagnostics
        ) noexcept {
            stats.walk_lookup.access += diagnostics.walk_lookup.access;
            stats.walk_lookup.transfer += diagnostics.walk_lookup.transfer;
            stats.walk_lookup.egress += diagnostics.walk_lookup.egress;
            stats.walk_lookup.skipped_by_phase += diagnostics.walk_lookup.skipped_by_phase;
            stats.walk_lookup.skipped_by_transfer_budget +=
                diagnostics.walk_lookup.skipped_by_transfer_budget;
            stats.paper_timed_lookup_skipped_phase +=
                diagnostics.timed_lookup_skipped_phase;
            stats.paper_timed_lookup_skipped_transfer_budget +=
                diagnostics.timed_lookup_skipped_transfer_budget;
            stats.paper_timed_successor_rejected_time_domain +=
                diagnostics.timed_successor_rejected_time_domain;
            stats.paper_timed_successor_rejected_same_trip +=
                diagnostics.timed_successor_rejected_same_trip;
            stats.paper_timed_successor_rejected_same_line +=
                diagnostics.timed_successor_rejected_same_line;
            stats.paper_timed_successor_rejected_feasibility +=
                diagnostics.timed_successor_rejected_feasibility;
        }

        void record_paper_successor_feasibility_rejection(
              TaskSearchStats&                   stats
            , PaperSuccessorFeasibilityRejection rejection
        ) noexcept {
            switch (rejection) {
                case PaperSuccessorFeasibilityRejection::None:
                    return;

                case PaperSuccessorFeasibilityRejection::FirstDepartureDomain:
                    ++stats.rejected_time_domain;
                    return;

                case PaperSuccessorFeasibilityRejection::BranchFeasibility:
                    ++stats.rejected_feasibility;
                    return;

                case PaperSuccessorFeasibilityRejection::Reboarding:
                    ++stats.rejected_reboarding;
                    return;
            }
        }

        template <typename Visitor, typename RejectedWalkVisitor>
        void for_each_successor(
              const PreprocessedNetwork& network
            , const DayLevelSupplySearchGraph* day_graph
            , ZoneId                      origin
            , const ActiveDestinationMembership& active_destinations
            , const SearchBranch&        branch
            , const TransferLimits&      limits
            , const SearchTimeDomain*    first_departure_domain
            , TaskSearchStats*           stats
            , Visitor&&                  visit
            , RejectedWalkVisitor&&      reject_walk
        ) {
            auto&& visitor = visit;
            auto&& walk_rejection_visitor = reject_walk;
            if (day_graph != nullptr) {
                for_each_day_level_supply_successor(
                      *day_graph
                    , network
                    , origin
                    , active_destinations
                    , branch
                    , limits
                    , first_departure_domain
                    , visitor
                    , walk_rejection_visitor
                );
                return;
            }

            auto diagnostics = PaperSuccessorGenerationDiagnostics{};
            for_each_paper_successor(
                  network
                , origin
                , active_destinations
                , branch
                , limits
                , first_departure_domain
                , stats != nullptr ? &diagnostics : nullptr
                , visitor
            );
            if (stats != nullptr) {
                add_paper_successor_generation_diagnostics(*stats, diagnostics);
            }
            (void)walk_rejection_visitor;
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

            const auto pruning_decision = evaluate_search_pruning(
                  pruning_execution
                , metrics
                , it->second
                , params.transfers
            );
            if (!pruning_decision.accepted) {
                switch (pruning_decision.layer) {
                    case SearchPruningLayer::Exact:
                        ++pruning_stats.rejected_exact;
                        break;
                    case SearchPruningLayer::Approximate:
                        ++pruning_stats.rejected_approximate;
                        break;
                }
                return pruning_decision;
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
            return pruning_decision;
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

        struct PaperConnectionRetentionDecision final {
            SearchPruningDecision pruning{};
            PaperConnectionLabelId label{};
            std::vector<PaperConnectionLabelId> removed_labels{};
            std::size_t removed_stale_labels{};

            [[nodiscard]] bool accepted() const noexcept {
                return pruning.accepted;
            }
        };

        mathfp::Expected<PaperConnectionRetentionDecision> retain_paper_connection_tree_node(
              PaperConnectionNodeKey            node
            , PartialPruningMetrics             metrics
            , std::optional<PaperConnectionLabelId> parent_label
            , PaperConnectionLabelRegistry&      label_registry
            , TreePartialRetention&             retention
            , const SearchParams&               params
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            //tex:
            // Paper $$C_y$$ retention at the current tree node. A candidate
            // $$c_y^*=c_x^*+s^*_{x,y}$$ is accepted only if no retained
            // $$c\in C_y$$ dominates it by $$DEP,ARR,IMP,NT$$ and the node-local
            // tolerance bounds for $$IMP,JT,NT$$ and $$MAXNT$$ are satisfied.
            // The map is created per origin batch, so $$C_y$$ contains only
            // connections that start from the same origin.
            ++pruning_stats.evaluated_candidates;
            auto it = retention.paper_connections.find(node);
            if (it == retention.paper_connections.end()) {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                std::vector<PaperConnectionLabelId> removed_labels;
                if (stores_search_pruning_metrics(pruning_execution)) {
                    insert_pruning_metrics(
                          retention.paper_connections
                        , pruning_execution
                        , node
                        , std::move(metrics)
                        , label
                        , removed_labels
                    );
                    ++pruning_stats.inserted_metrics;
                } else {
                    ++pruning_stats.skipped_insertions;
                }
                ++pruning_stats.accepted_candidates;
                return PaperConnectionRetentionDecision{
                      .pruning = SearchPruningDecision{
                          .layer    = SearchPruningLayer::Exact
                        , .reason   = SearchPruningReason::Accepted
                        , .accepted = true
                      }
                    , .label = label
                    , .removed_labels = std::move(removed_labels)
                };
            }

            const auto removed_stale_labels =
                remove_inactive_paper_node_connection_metrics(
                      it->second
                    , label_registry
                );
            if (it->second.empty()) {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                std::vector<PaperConnectionLabelId> removed_labels;
                if (stores_search_pruning_metrics(pruning_execution)) {
                    insert_pruning_metrics(
                          retention.paper_connections
                        , pruning_execution
                        , node
                        , std::move(metrics)
                        , label
                        , removed_labels
                    );
                    ++pruning_stats.inserted_metrics;
                } else {
                    ++pruning_stats.skipped_insertions;
                }
                ++pruning_stats.accepted_candidates;
                return PaperConnectionRetentionDecision{
                      .pruning = SearchPruningDecision{
                          .layer    = SearchPruningLayer::Exact
                        , .reason   = SearchPruningReason::Accepted
                        , .accepted = true
                      }
                    , .label = label
                    , .removed_labels = std::move(removed_labels)
                    , .removed_stale_labels = removed_stale_labels
                };
            }

            const auto pruning_decision = evaluate_paper_node_connection_set(
                  pruning_execution
                , metrics
                , it->second
                , params.transfers
            );
            if (!pruning_decision.accepted) {
                switch (pruning_decision.layer) {
                    case SearchPruningLayer::Exact:
                        ++pruning_stats.rejected_exact;
                        break;
                    case SearchPruningLayer::Approximate:
                        ++pruning_stats.rejected_approximate;
                        break;
                }
                return PaperConnectionRetentionDecision{
                      .pruning = pruning_decision
                    , .removed_stale_labels = removed_stale_labels
                };
            }
            if (stores_search_pruning_metrics(pruning_execution)) {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                std::vector<PaperConnectionLabelId> removed_labels;
                insert_pruning_metrics(
                      retention.paper_connections
                    , pruning_execution
                    , node
                    , std::move(metrics)
                    , label
                    , removed_labels
                );
                ++pruning_stats.inserted_metrics;
                pruning_stats.accepted_candidates++;
                return PaperConnectionRetentionDecision{
                      .pruning = pruning_decision
                    , .label = label
                    , .removed_labels = std::move(removed_labels)
                    , .removed_stale_labels = removed_stale_labels
                };
            } else {
                const auto label = allocate_paper_connection_label(
                      label_registry
                    , parent_label
                );
                ++pruning_stats.skipped_insertions;
                ++pruning_stats.accepted_candidates;
                return PaperConnectionRetentionDecision{
                      .pruning = pruning_decision
                    , .label = label
                    , .removed_stale_labels = removed_stale_labels
                };
            }
        }

        mathfp::Expected<SearchPruningDecision> retain_od_day_label_branch(
              const SearchBranch&               branch
            , TreePartialRetention&             retention
            , const SearchParams&               params
            , const SearchCostContext&          search_cost
            , const SearchPruningExecutionPlan& pruning_execution
            , SearchPruningRuntimeStats&        pruning_stats
        ) {
            ++pruning_stats.evaluated_candidates;
            MATHFP_TRY_LET(
                  PartialPruningMetrics
                , metrics
                , make_day_path_pruning_metrics(branch, search_cost)
            );
            const auto& support = branch.od_day_carrier.support_envelope;
            auto key = od_day_label_state(branch);
            auto it = retention.od_day_label_states.find(key);
            if (it == retention.od_day_label_states.end()) {
                if (stores_search_pruning_metrics(pruning_execution)) {
                    auto representatives = OdDayLabelRepresentativeSet{};
                    insert_od_day_label_representative(
                          pruning_execution
                        , representatives
                        , metrics
                        , support
                    );
                    retention.od_day_label_states.emplace(std::move(key), std::move(representatives));
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

            const auto compatible_metrics = compatible_od_day_label_metrics(
                  it->second
                , support
            );
            auto pruning_decision = SearchPruningDecision{
                  .layer    = SearchPruningLayer::Exact
                , .reason   = SearchPruningReason::Accepted
                , .accepted = true
            };
            if (!compatible_metrics.metrics.empty()) {
                pruning_decision = evaluate_search_pruning(
                      pruning_execution
                    , metrics
                    , compatible_metrics
                    , params.transfers
                );
            }
            if (!pruning_decision.accepted) {
                switch (pruning_decision.layer) {
                    case SearchPruningLayer::Exact:
                        ++pruning_stats.rejected_exact;
                        break;
                    case SearchPruningLayer::Approximate:
                        ++pruning_stats.rejected_approximate;
                        break;
                }
                return pruning_decision;
            }
            if (contains_od_day_label_representative(it->second, metrics, support)) {
                ++pruning_stats.rejected_exact;
                return SearchPruningDecision{
                      .layer    = SearchPruningLayer::Exact
                    , .reason   = SearchPruningReason::RejectedExactDominance
                    , .accepted = false
                };
            }

            if (stores_search_pruning_metrics(pruning_execution)) {
                insert_od_day_label_representative(
                      pruning_execution
                    , it->second
                    , metrics
                    , support
                );
                ++pruning_stats.inserted_metrics;
            } else {
                ++pruning_stats.skipped_insertions;
            }
            ++pruning_stats.accepted_candidates;
            return pruning_decision;
        }

        [[nodiscard]] std::string format_optional_size_limit(
            std::optional<std::size_t> limit
        ) {
            return limit.has_value()
                ? std::to_string(*limit)
                : std::string("unbounded");
        }

        [[nodiscard]] std::string format_optional_mb_limit(
            std::optional<std::size_t> limit
        ) {
            return limit.has_value()
                ? fmt::format("{:.2f}", static_cast<double>(*limit) / (1024.0 * 1024.0))
                : std::string("unbounded");
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

        [[nodiscard]] std::map<ZoneId, std::size_t> projection_slot_position_by_destination(
            std::span<const SearchProjectionSlot> batch_slots
        ) {
            std::map<ZoneId, std::size_t> positions;
            for (std::size_t slot_pos = 0; slot_pos < batch_slots.size(); ++slot_pos) {
                positions[batch_slots[slot_pos].destination] = slot_pos;
            }
            return positions;
        }

        void add_complete_projection_retention_diagnostics(
              TaskSearchStats&                                stats
            , const CompleteProjectionRetentionDiagnostics&    diagnostics
        ) noexcept {
            stats.completed_connections += diagnostics.completed_connections;
            stats.rejected_complete_admissibility +=
                diagnostics.rejected_complete_admissibility;
            stats.rejected_complete_dominance +=
                diagnostics.rejected_complete_dominance;
            stats.removed_complete_dominated +=
                diagnostics.removed_complete_dominated;
            stats.post_layer_day_path_candidates +=
                diagnostics.post_layer_day_path_candidates;
            stats.post_layer_day_path_inserted +=
                diagnostics.post_layer_day_path_inserted;
            stats.post_layer_day_path_representative_replaced +=
                diagnostics.post_layer_day_path_representative_replaced;
            stats.post_layer_day_path_supports = std::max(
                  stats.post_layer_day_path_supports
                , diagnostics.post_layer_day_path_supports
            );
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
            , const DayLevelSupplySearchGraph*  day_level_supply = nullptr
            , const SearchCancellationToken*    cancellation = nullptr
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
            MATHFP_TRY(validate_od_day_production_batch_contract(
                  batch
                , partial_retention_scope
                , pruning_execution
                , day_level_supply != nullptr
            ));

            std::vector<SearchProjectionRetention> retentions;
            retentions.reserve(batch.projection_slots.size());
            for (const auto& slot : batch.projection_slots) {
                retentions.push_back(SearchProjectionRetention{ .slot = slot });
            }
            TreePartialRetention tree_partial_retention{};
            PaperConnectionLabelRegistry paper_label_registry{};

            TaskSearchStats                   stats;
            std::vector<TaskSearchStats>      task_stats(batch.projection_slots.size());
            BranchArena                       branches;
            std::deque<std::optional<FixedActiveMask>> completion_projection_states;
            std::deque<std::optional<DemandBranchProjectionState>> demand_projection_states;
            std::size_t                       released_branches = 0;
            const SearchTimeDomain*           first_departure_domain =
                &batch.departure_domain.get();
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
            const auto od_day_slots =
                od_day_projection_slots(batch_task_span);
            const auto* od_day_supply = od_day_slots ? nullptr : day_level_supply;
            const auto target_positions_by_destination = target_projection_slots
                ? completion_target_position_by_destination(batch_target_span)
                : std::map<ZoneId, std::size_t>{};
            const auto od_day_slot_positions_by_destination = od_day_slots
                ? projection_slot_position_by_destination(batch_task_span)
                : std::map<ZoneId, std::size_t>{};
            std::unordered_set<std::int64_t> od_day_destination_ids;
            if (od_day_slots) {
                od_day_destination_ids.reserve(batch.completion_targets.size());
                for (const auto& target : batch.completion_targets) {
                    od_day_destination_ids.insert(target.destination.get());
                }
            }
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
            std::optional<ResidualReachability> reachability;
            std::optional<ReachabilityMaskCache> reachability_cache;
            if (!od_day_slots) {
                reachability.emplace(build_residual_reachability(
                      reverse_graph
                    , batch_target_span
                    , params.transfers.max_transfers
                    , search_cost.impedance
                    , search_cost.fare_scale
                ));
                MATHFP_TRY(validate_residual_reachability(
                      *reachability
                    , params.transfers.max_transfers
                ));
                reachability_cache.emplace(
                    ReachabilityMaskCache{
                          .reachability   = *reachability
                        , .targets        = batch_target_span
                        , .slots          = batch_task_span
                        , .max_transfers  = params.transfers.max_transfers
                        , .unified_completion_targets = target_projection_slots
                    }
                );
            }

            SearchFrontierLayer current_frontier;
            SearchFrontierLayer next_frontier;
            //tex:
            // The tree is traversed with two frontier buffers. `current_frontier`
            // is exhausted before `next_frontier` becomes current. In this
            // implementation the level is transfer-depth oriented: walk legs and
            // the first boarding stay in the current level, while a later timed
            // boarding after a transfer moves the branch to the next level.
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
                  , .od_day_carrier = OdDayProductionCarrier{
                        .path_identity = make_od_day_path_prefix(batch.key.origin)
                    }
              }
            );
            if (diagnostics.validate_phase_invariants) {
                MATHFP_TRY(validate_search_branch_phase_invariants(
                    branch_at(branches, root_branch_index)
                ));
            }
            if (!od_day_slots && target_projection_slots) {
                const auto root_reachability_key = reachability_mask_key(
                      branch_at(branches, root_branch_index)
                    , params.transfers
                );
                auto root_reachable_targets = filter_target_positions_by_reachability(
                      ActiveIndexSet::full(batch.completion_targets.size())
                    , reachability_cache->target_entry(root_reachability_key)
                );
                auto root_reachability = filter_task_positions_by_reachability(
                      ActiveIndexSet::full(batch.projection_slots.size())
                    , reachability_cache->slot_entry(root_reachability_key)
                );
                record_reachability_rejections(
                      std::span<const RejectedReachabilityTask>{
                          root_reachability.unreachable.data()
                        , root_reachability.unreachable.size()
                      }
                    , task_stats
                    , stats
                );
                if (!root_reachability.reachable.equals(root_reachable_targets)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("completion-target root reachability masks disagree")
                            .ctx("origin", batch.key.origin.get())
                    );
                }
                completion_projection_states.push_back(
                    FixedActiveMask::from(root_reachability.reachable)
                );
            } else if (!od_day_slots) {
                const auto root_reachability_key = reachability_mask_key(
                      branch_at(branches, root_branch_index)
                    , params.transfers
                );
                auto root_reachable_targets = filter_target_positions_by_reachability(
                      ActiveIndexSet::full(batch.completion_targets.size())
                    , reachability_cache->target_entry(root_reachability_key)
                );
                auto root_reachability = filter_task_positions_by_reachability(
                      ActiveIndexSet::full(batch.projection_slots.size())
                    , reachability_cache->slot_entry(root_reachability_key)
                );
                record_reachability_rejections(
                      std::span<const RejectedReachabilityTask>{
                          root_reachability.unreachable.data()
                        , root_reachability.unreachable.size()
                      }
                    , task_stats
                    , stats
                );
                demand_projection_states.push_back(
                    DemandBranchProjectionState{
                          .active_tasks = std::move(root_reachability.reachable)
                        , .active_targets = std::move(root_reachable_targets)
                    }
                );
            }
            current_frontier.push_back(root_branch_index);
            BranchPhaseStats current_frontier_by_phase;
            BranchPhaseStats next_frontier_by_phase;
            increment_phase_stats(
                  current_frontier_by_phase
                , SearchBranchPhase::AtOrigin
            );
            auto retained_production_alternative_count = [&]() noexcept {
                return od_day_slots
                    ? retained_day_path_count(retentions)
                    : retained_connection_count(retentions);
            };

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
                    , retained_production_alternative_count()
                )
            );

            using Clock = std::chrono::steady_clock;
            const auto batch_started_at = Clock::now();
            auto last_wall_clock_heartbeat = batch_started_at;
            auto projection_state_count = [&]() noexcept {
                if (od_day_slots) {
                    return std::size_t{ 0u };
                }
                return target_projection_slots
                    ? completion_projection_states.size() - released_branches
                    : demand_projection_states.size() - released_branches;
            };
            const auto projection_state_size = od_day_slots
                ? std::size_t{ 0u }
                : (target_projection_slots
                    ? sizeof(FixedActiveMask)
                    : sizeof(DemandBranchProjectionState));
            const OdDayProductionMemoryLimits od_day_memory_limits{};
            auto make_storage_diagnostics = [&]() noexcept {
                return search_storage_diagnostics(
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
                );
            };
            auto emit_storage_diagnostics = [&]() {
                const auto storage_diagnostics = make_storage_diagnostics();
                log(format_search_storage_diagnostics(storage_diagnostics), LogLevel::Info);
                if (od_day_slots) {
                    log(
                        format_od_day_theory_diagnostics(
                              storage_diagnostics
                            , stats
                            , current_frontier.size()
                            , next_frontier.size()
                        )
                        , LogLevel::Info
                    );
                }
            };
            if (od_day_slots) {
                log(
                    fmt::format(
                          "OD-day production memory limits: carrier=compact_connection_segment_prefix branch_slots={} live_branches={} frontier={} legacy_od_day_label_states={} label_representatives=unbounded post_layer_day_paths={} approx_direct_mb={}"
                        , format_optional_size_limit(od_day_memory_limits.max_branch_slots_per_tree)
                        , format_optional_size_limit(od_day_memory_limits.max_live_branches_per_tree)
                        , format_optional_size_limit(od_day_memory_limits.max_frontier_per_tree)
                        , format_optional_size_limit(od_day_memory_limits.max_od_day_label_states_per_tree)
                        , format_optional_size_limit(od_day_memory_limits.max_retained_day_paths_per_tree)
                        , format_optional_mb_limit(od_day_memory_limits.max_approximate_direct_bytes_per_tree)
                    )
                    , LogLevel::Info
                );
            }
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
                              " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={})"
                              " frontier_phase(current={}, next={})"
                              " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
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
                            , retained_production_alternative_count()
                            , current_frontier.size()
                            , next_frontier.size()
                            , stats.stale_frontier_skipped
                            , stats.c_y_removed_dominated
                            , stats.c_y_removed_stale
                            , stats.frontier_compaction_runs
                            , stats.frontier_compaction_removed
                            , format_branch_phase_stats(current_frontier_by_phase)
                            , format_branch_phase_stats(next_frontier_by_phase)
                            , format_walk_lookup_stats(stats.walk_lookup)
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
                if (od_day_slots) {
                    ++released_branches;
                    return;
                }
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
            constexpr auto kOdDayFrontierCompactionMinRemoved = std::size_t{ 1024u };
            constexpr auto kOdDayFrontierCompactionMinSize    = std::size_t{ 4096u };
            auto c_y_removed_since_frontier_compaction = std::size_t{ 0u };
            auto compact_frontier_queue =
                [&](SearchFrontierLayer& queue, BranchPhaseStats& phase_stats) {
                    auto kept = std::vector<std::size_t>{};
                    kept.reserve(queue.size());
                    auto removed = std::size_t{ 0u };
                    for (auto i = queue.head; i < queue.entries.size(); ++i) {
                        const auto queued_index = queue.entries[i];
                        if (!branch_alive(branches, queued_index)) {
                            ++removed;
                            continue;
                        }
                        const auto& queued_branch = branch_at(branches, queued_index);
                        if (paper_connection_label_active(
                              paper_label_registry
                            , queued_branch.paper_connection_label
                        )) {
                            kept.push_back(queued_index);
                            continue;
                        }
                        decrement_phase_stats(phase_stats, queued_branch.trace.phase);
                        release_branch_if_closed(
                              branches
                            , queued_index
                            , release_projection_payload
                        );
                        ++removed;
                    }
                    queue.entries = std::move(kept);
                    queue.head = 0u;
                    return removed;
                };
            auto compact_od_day_frontiers = [&](const char* reason) {
                if (!od_day_slots) {
                    return;
                }
                const auto removed_current = compact_frontier_queue(
                      current_frontier
                    , current_frontier_by_phase
                );
                const auto removed_next = compact_frontier_queue(
                      next_frontier
                    , next_frontier_by_phase
                );
                const auto removed = removed_current + removed_next;
                if (removed == 0u) {
                    return;
                }
                ++stats.frontier_compaction_runs;
                stats.frontier_compaction_removed += removed;
                stats.frontier_compaction_removed_current += removed_current;
                stats.frontier_compaction_removed_next += removed_next;
                log(
                    fmt::format(
                          "OD-day frontier compacted: origin={} reason={} removed={} current_removed={} next_removed={} frontier={}/{}"
                        , batch.key.origin.get()
                        , reason
                        , removed
                        , removed_current
                        , removed_next
                        , current_frontier.size()
                        , next_frontier.size()
                    )
                    , LogLevel::Debug
                );
            };
            const auto paper_connection_tree_targets =
                od_day_slots
                    ? ActiveIndexSet::full(batch.completion_targets.size())
                    : ActiveIndexSet{};

            while (!current_frontier.empty() || !next_frontier.empty()) {
                if (search_cancelled(cancellation)) {
                    log(
                        fmt::format(
                              "search batch cancelled: {}/{} origin={} interval={} expanded={} accepted={} found={} reason=sibling_failed"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_production_alternative_count()
                        )
                        , LogLevel::Warning
                    );
                    return std::vector<SearchSlotResult>{};
                }
                if (current_frontier.empty()) {
                    compact_od_day_frontiers("level_swap");
                    c_y_removed_since_frontier_compaction = 0u;
                    current_frontier.swap(next_frontier);
                    current_frontier_by_phase = next_frontier_by_phase;
                    next_frontier_by_phase = BranchPhaseStats{};
                    if (current_frontier.empty()) {
                        continue;
                    }
                }

                stats.max_current_frontier = std::max(stats.max_current_frontier, current_frontier.size());
                stats.max_next_frontier    = std::max(stats.max_next_frontier   , next_frontier   .size());

                const auto branch_index = current_frontier.front();
                current_frontier.pop_front();
                const auto& branch = branch_at(branches, branch_index);
                decrement_phase_stats(current_frontier_by_phase, branch.trace.phase);
                if (od_day_slots
                    && !paper_connection_label_active(
                          paper_label_registry
                        , branch.paper_connection_label
                    )) {
                    ++stats.stale_frontier_skipped;
                    release_branch_if_closed(
                          branches
                        , branch_index
                        , release_projection_payload
                    );
                    continue;
                }
                emit_wall_clock_heartbeat("branch", branch_index);
                ActiveIndexSet completion_active;
                const ActiveIndexSet* active_tasks_ptr{};
                const ActiveIndexSet* active_targets_ptr{};
                if (od_day_slots) {
                    active_tasks_ptr   = &paper_connection_tree_targets;
                    active_targets_ptr = &paper_connection_tree_targets;
                } else if (target_projection_slots) {
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
                const auto active_destinations = od_day_slots
                    ? ActiveDestinationMembership{
                          .direct_destination_ids = &od_day_destination_ids
                      }
                    : ActiveDestinationMembership{
                          .active_targets = &active_targets
                        , .batch_targets  = batch_target_span
                      };
                ++stats.expanded_branches;
                if (od_day_slots && ((stats.expanded_branches % 1024u) == 0u)) {
                    MATHFP_TRY(validate_od_day_production_memory_limits(
                          make_storage_diagnostics()
                        , current_frontier.size()
                        , next_frontier.size()
                        , od_day_memory_limits
                        , batch.key.origin
                    ));
                }

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
                            , retained_production_alternative_count()
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
                              " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={})"
                              " paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{}"
                              " late_guard(time_domain/feasibility/reboarding)={}/{}/{}"
                              " frontier_phase(current={}, next={})"
                              " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
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
                            , retained_production_alternative_count()
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
                            , stats.stale_frontier_skipped
                            , stats.c_y_removed_dominated
                            , stats.c_y_removed_stale
                            , stats.frontier_compaction_runs
                            , stats.frontier_compaction_removed
                            , stats.paper_timed_lookup_skipped_phase
                            , stats.paper_timed_lookup_skipped_transfer_budget
                            , stats.paper_timed_successor_rejected_time_domain
                            , stats.paper_timed_successor_rejected_same_trip
                            , stats.paper_timed_successor_rejected_same_line
                            , stats.paper_timed_successor_rejected_feasibility
                            , stats.rejected_time_domain
                            , stats.rejected_feasibility
                            , stats.rejected_reboarding
                            , format_branch_phase_stats(current_frontier_by_phase)
                            , format_branch_phase_stats(next_frontier_by_phase)
                            , format_walk_lookup_stats(stats.walk_lookup)
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
                bool batch_cancelled = false;
                for_each_successor(
                      network
                    , od_day_supply
                    , batch.key.origin
                    , active_destinations
                    , branch
                    , params.transfers
                    , first_departure_domain
                    , &stats
                    , [&](const SearchSuccessor& successor_ref) {
                    if (search_cancelled(cancellation)) {
                        batch_cancelled = true;
                        return;
                    }
                    if (!successor_error) {
                        return;
                    }
                    ++stats.generated_successors;
                    if (successor_ref.walk_transition.has_value()) {
                        increment_walk_kind_stats(stats.generated_walk, successor_ref.walk_transition->kind);
                    }
                    if ((stats.generated_successors % kSearchWallClockSuccessorCheckStep) == 0) {
                        emit_wall_clock_heartbeat("successor", branch_index);
                    }
                    const auto& successor = connection_segment_at(network, successor_ref.connection);
                    if (!od_day_slots || !successor_ref.support_envelope.has_value()) {
                        /*
                         * In production OD-day this is an invariant guard: the
                         * generator must have emitted only insertable paper
                         * successors. Legacy timed search keeps the predicate
                         * as its working filter.
                         */
                        const auto feasibility_decision =
                            evaluate_paper_search_successor_feasibility(
                                  branch
                                , network
                                , successor_ref
                                , first_departure_domain
                                , params.transfers
                            );
                        if (!feasibility_decision.accepted()) {
                            record_paper_successor_feasibility_rejection(
                                  stats
                                , feasibility_decision.rejection
                            );
                            if (od_day_slots) {
                                successor_error = mathfp::unexpected(
                                    mathfp::internal_error("OD-day paper successor failed late insertability invariant")
                                        .ctx("origin", batch.key.origin.get())
                                        .ctx("branch", static_cast<std::int64_t>(branch_index))
                                        .ctx("connection", static_cast<std::int64_t>(successor_ref.connection.get()))
                                        .ctx("rejection", static_cast<std::int64_t>(feasibility_decision.rejection))
                                );
                            }
                            return;
                        }
                    }

                    std::optional<PaperConnectionLabelId> accepted_paper_label;
                    if (od_day_slots
                        && partial_retention_scope
                            == SearchPartialRetentionScope::TreeGlobal) {
                        auto paper_prefix =
                            evaluate_paper_connection_prefix_before_branch(
                                  branches
                                , branch
                                , network
                                , successor_ref
                                , batch.key.interval
                                , search_cost
                            );
                        if (!paper_prefix) {
                            successor_error = mathfp::unexpected(
                                std::move(paper_prefix.error())
                            );
                            return;
                        }
                        if (!paper_prefix->connection_candidate.has_value()) {
                            if (paper_prefix->rejection == BranchTransitionRejection::RepeatedPhysicalNode
                                || paper_prefix->rejection == BranchTransitionRejection::RepeatedStopOccurrence) {
                                ++stats.rejected_cycles;
                                return;
                            }
                            if (!paper_prefix->accepted()) {
                                return;
                            }
                        } else {
                            if (paper_prefix->connection_candidate->metrics.transfers
                                > params.transfers.max_transfers) {
                                ++stats.rejected_transfer_limit;
                                return;
                            }

                            auto pruning_decision = retain_paper_connection_tree_node(
                                  paper_prefix->connection_candidate->node
                                , std::move(paper_prefix->connection_candidate->metrics)
                                , branch.paper_connection_label
                                , paper_label_registry
                                , tree_partial_retention
                                , params
                                , pruning_execution
                                , stats.pruning
                            );
                            if (!pruning_decision) {
                                successor_error = mathfp::unexpected(
                                    std::move(pruning_decision.error())
                                );
                                return;
                            }
                            if (!pruning_decision->accepted()) {
                                ++stats.rejected_dominance_or_tolerance;
                                return;
                            }
                            for (const auto removed_label : pruning_decision->removed_labels) {
                                deactivate_paper_connection_label(
                                      paper_label_registry
                                    , removed_label
                                );
                            }
                            const auto removed_from_c_y =
                                  pruning_decision->removed_labels.size()
                                + pruning_decision->removed_stale_labels;
                            stats.c_y_removed_dominated +=
                                pruning_decision->removed_labels.size();
                            stats.c_y_removed_stale +=
                                pruning_decision->removed_stale_labels;
                            c_y_removed_since_frontier_compaction += removed_from_c_y;
                            if (c_y_removed_since_frontier_compaction
                                    >= kOdDayFrontierCompactionMinRemoved
                                && (current_frontier.size() + next_frontier.size())
                                    >= kOdDayFrontierCompactionMinSize) {
                                compact_od_day_frontiers("c_y_removal");
                                c_y_removed_since_frontier_compaction = 0u;
                            }
                            accepted_paper_label = pruning_decision->label;
                        }
                    }

                    auto candidate_result = transition_search_branch(
                          branches
                        , branch_index
                        , branch
                        , network
                        , successor_ref
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
                    candidate->od_day_carrier = project_od_day_carrier_transition(
                          branch.od_day_carrier
                        , successor_ref
                        , successor
                        , route_segment_at(network, successor.route_segment)
                    );
                    if (od_day_slots) {
                        /*
                         * Production OD-day candidates carry their structural
                         * path and timed witness in compact prefixes. Detaching
                         * the trace parent before any retention/materialization
                         * keeps the production contour from falling back to
                         * raw timed prefix chains.
                         */
                        candidate->trace.parent_branch = std::nullopt;
                        candidate->paper_connection_label = accepted_paper_label;
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
                    if (od_day_slots
                        && candidate->metrics.departure.has_value()
                        && candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        const auto position_it = od_day_slot_positions_by_destination.find(
                            ZoneId{ candidate->trace.current_physical.id }
                        );
                        if (position_it != od_day_slot_positions_by_destination.end()) {
                            completed_target = true;
                            complete_task_positions.push_back(position_it->second);
                        }
                    } else if (target_projection_slots
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
                            auto complete_result = retain_complete_projection_for_slot(
                                  *candidate
                                , branches
                                , network
                                , params.transfers
                                , search_cost
                                , batch.projection_slots[task_pos]
                                , assignment_period
                                , admissibility_config
                                , complete_connection_dominance
                                , retentions[task_pos]
                            );
                            if (!complete_result) {
                                successor_error = mathfp::unexpected(
                                    std::move(complete_result.error())
                                );
                                return;
                            }
                            add_complete_projection_retention_diagnostics(
                                  task_stats[task_pos]
                                , *complete_result
                            );
                            add_complete_projection_retention_diagnostics(
                                  stats
                                , *complete_result
                            );
                            retained_complete =
                                retained_complete
                                || complete_result->completed_connections > 0u;
                        }
                        if (retained_complete && successor_ref.walk_transition.has_value()) {
                            increment_walk_kind_stats(
                                  stats.accepted_walk
                                , successor_ref.walk_transition->kind
                            );
                        }
                        return;
                    }

                    if (candidate->trace.current_physical.kind == EndpointKind::Zone) {
                        ++stats.rejected_dominance_or_tolerance;
                        return;
                    }

                    if (od_day_slots) {
                        ++stats.accepted_branches;
                        if (successor_ref.walk_transition.has_value()) {
                            increment_walk_kind_stats(
                                  stats.accepted_walk
                                , successor_ref.walk_transition->kind
                            );
                        }
                        increment_phase_stats(
                              stats.accepted_branches_by_phase
                            , candidate->trace.phase
                        );
                        const auto candidate_phase = candidate->trace.phase;
                        const auto candidate_index = append_branch(
                              branches
                            , std::move(*candidate)
                        );
                        next_frontier.push_back(candidate_index);
                        increment_phase_stats(next_frontier_by_phase, candidate_phase);
                        if (od_day_memory_limits.max_frontier_per_tree.has_value()
                            && (current_frontier.size() + next_frontier.size()
                                > *od_day_memory_limits.max_frontier_per_tree)) {
                            successor_error = validate_od_day_production_memory_limits(
                                  make_storage_diagnostics()
                                , current_frontier.size()
                                , next_frontier.size()
                                , od_day_memory_limits
                                , batch.key.origin
                            );
                        }
                        return;
                    }

                    const auto candidate_reachability_key = reachability_mask_key(
                          *candidate
                        , params.transfers
                    );
                    const auto& target_reachability_entry =
                        reachability_cache->target_entry(candidate_reachability_key);
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

                    if (!od_day_slots
                        && partial_retention_scope
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
                        , reachability_cache->slot_entry(candidate_reachability_key)
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
                        if (batch.projection_slots[task_pos].kind
                            == SearchProjectionSlotKind::OdDayPair) {
                            next_active_tasks.set(task_pos);
                            return;
                        }
                        auto lower_bound_decision = target_projection_slots
                            ? evaluate_suffix_lower_bound_pruning(
                                  *candidate
                                , batch.projection_slots[task_pos].destination
                                , *reachability
                                , retentions[task_pos].compact_complete_connections
                                , params
                                , search_cost
                                , choice_config
                                , complete_connection_dominance
                              )
                            : evaluate_suffix_lower_bound_pruning(
                                  *candidate
                                , batch.projection_slots[task_pos].destination
                                , *reachability
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
                    if (successor_ref.walk_transition.has_value()) {
                        increment_walk_kind_stats(
                              stats.accepted_walk
                            , successor_ref.walk_transition->kind
                        );
                    }
                    increment_phase_stats(
                          stats.accepted_branches_by_phase
                        , candidate->trace.phase
                    );
                    const auto same_level =
                        is_walk_connection(successor) || !branch.metrics.departure.has_value();
                    //tex:
                    // Frontier placement follows the transfer-depth level used
                    // above: always-available walk segments and the first timed
                    // boarding do not increase $$NT$$, while subsequent timed
                    // boardings represent the next transfer level.
                    const auto candidate_phase = candidate->trace.phase;
                    const auto candidate_index = append_branch(
                          branches
                        , std::move(*candidate)
                    );
                    if (target_projection_slots) {
                        completion_projection_states.push_back(
                            FixedActiveMask::from(next_active_tasks)
                        );
                    } else {
                        demand_projection_states.push_back(
                            DemandBranchProjectionState{
                                  .active_tasks = std::move(next_active_tasks)
                                , .active_targets = std::move(next_active_targets)
                            }
                        );
                    }
                    if (same_level) {
                        current_frontier.push_back(candidate_index);
                        increment_phase_stats(current_frontier_by_phase, candidate_phase);
                    } else {
                        next_frontier   .push_back(candidate_index);
                        increment_phase_stats(next_frontier_by_phase, candidate_phase);
                    }
                    if (od_day_slots
                        && od_day_memory_limits.max_frontier_per_tree.has_value()
                        && (current_frontier.size() + next_frontier.size()
                            > *od_day_memory_limits.max_frontier_per_tree)) {
                        successor_error = validate_od_day_production_memory_limits(
                              make_storage_diagnostics()
                            , current_frontier.size()
                            , next_frontier.size()
                            , od_day_memory_limits
                            , batch.key.origin
                        );
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
                if (batch_cancelled) {
                    log(
                        fmt::format(
                              "search batch cancelled during successor scan: {}/{} origin={} interval={} expanded={} accepted={} found={} reason=sibling_failed"
                            , batch_index + 1
                            , batch_count
                            , batch.key.origin.get()
                            , format_batch_interval(batch.key.interval)
                            , stats.expanded_branches
                            , stats.accepted_branches
                            , retained_production_alternative_count()
                        )
                        , LogLevel::Warning
                    );
                    return std::vector<SearchSlotResult>{};
                }
                MATHFP_TRY(std::move(successor_error));
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

            std::vector<SearchSlotResult> slot_results;
            slot_results.reserve(batch.projection_slots.size());
            std::size_t batch_final_found = 0;
            std::size_t batch_retained_before_tolerance = 0;
            for (std::size_t task_pos = 0; task_pos < batch.projection_slots.size(); ++task_pos) {
                const auto& slot = batch.projection_slots[task_pos];
                auto& retention   = retentions[task_pos];
                MATHFP_TRY(validate_od_day_post_layer_retention(slot, retention));
                const auto before_tolerance = target_projection_slots
                    ? retention.compact_complete_connections.metrics.size()
                    : slot.kind == SearchProjectionSlotKind::OdDayPair
                        ? day_path_retention_size(retention.day_paths)
                        : retention.complete_connections.alternatives.size();
                std::vector<SearchConnection> connections;
                std::vector<DayPathAlternative> day_path_alternatives;
                std::size_t connection_count = 0u;
                if (target_projection_slots) {
                    connection_count = finalize_compact_complete_connection_count(
                          retention.compact_complete_connections
                        , params.choice_tolerances
                        , choice_config.rollout_stage
                    );
                } else {
                    if (slot.kind == SearchProjectionSlotKind::OdDayPair) {
                        /*
                         * Production OD-day search finalizes only OD path
                         * alternatives. Raw completed SearchConnection objects
                         * remain support payload under DayPathAlternative and
                         * must not become the slot result.
                         */
                        (void)choice_config;
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
                            , params.choice_tolerances
                            , choice_config.rollout_stage
                        );
                        connection_count = connections.size();
                    }
                    task_stats[task_pos].rejected_complete_tolerance =
                        before_tolerance - connection_count;
                }
                if (target_projection_slots) {
                    task_stats[task_pos].rejected_complete_tolerance =
                        before_tolerance - connection_count;
                }
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
                        , .day_path_alternatives = std::move(day_path_alternatives)
                    }
                );
                MATHFP_TRY(validate_od_day_post_layer_result(slot_results.back()));
                MATHFP_TRY(validate_reachability_rejection_stats(task_stats[task_pos]));
                MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(task_stats[task_pos]));

                if (diagnostics.log_projection_details) {
                    log(
                        fmt::format(
                              "search batch projection done: batch={}/{} slot={} kind={} task={} origin={} destination={} interval={} found={:>8}"
                              " retained_before_tolerance={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                              " reachability_pruned={} reachability_detail(phase/budget/unreachable)={}/{}/{}"
                              " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                            , batch_index + 1
                            , batch_count
                            , static_cast<std::int64_t>(task_pos)
                            , to_log_token(slot.kind)
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
            }
            MATHFP_TRY(validate_reachability_rejection_stats(stats));
            MATHFP_TRY(validate_suffix_lower_bound_rejection_stats(stats));

            log(
                fmt::format(
                      "search batch done: {}/{} origin={} interval={} tasks={} found={:>8} completed={:>8}"
                      " retained_before_tolerance={:>8} complete_rejected(admissibility/dominance/tolerance)={}/{}/{} complete_removed_dominated={}"
                      " expanded={:>8} generated={:>8} accepted={:>8}"
                      " rejected(time_domain/feasibility/reboarding/cycles/limit/reachability/dominance)={}/{}/{}/{}/{}/{}/{}"
                      " reachability_detail(phase/budget/unreachable)={}/{}/{} max_frontier={}/{}"
                      " lower_bound_pruned={} lower_bound_detail(exact/imp/jt/nt)={}/{}/{}/{}"
                      " frontier_sync(stale_skipped={} c_y_removed_dominated={} c_y_removed_stale={} compact_runs={} compact_removed={} compact_current={} compact_next={})"
                      " post_layer(candidates/inserted/replaced/max_supports)={}/{}/{}/{}"
                      " paper_lookup_pruned(phase/budget/time_domain/same_trip/same_line/feasibility)={}/{}/{}/{}/{}/{}"
                      " late_guard(time_domain/feasibility/reboarding)={}/{}/{}"
                      " walk_lookup({}) walk_generated({}) walk_accepted({}) rejected_consecutive_walk={}"
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
                    , stats.stale_frontier_skipped
                    , stats.c_y_removed_dominated
                    , stats.c_y_removed_stale
                    , stats.frontier_compaction_runs
                    , stats.frontier_compaction_removed
                    , stats.frontier_compaction_removed_current
                    , stats.frontier_compaction_removed_next
                    , stats.post_layer_day_path_candidates
                    , stats.post_layer_day_path_inserted
                    , stats.post_layer_day_path_representative_replaced
                    , stats.post_layer_day_path_supports
                    , stats.paper_timed_lookup_skipped_phase
                    , stats.paper_timed_lookup_skipped_transfer_budget
                    , stats.paper_timed_successor_rejected_time_domain
                    , stats.paper_timed_successor_rejected_same_trip
                    , stats.paper_timed_successor_rejected_same_line
                    , stats.paper_timed_successor_rejected_feasibility
                    , stats.rejected_time_domain
                    , stats.rejected_feasibility
                    , stats.rejected_reboarding
                    , format_walk_lookup_stats(stats.walk_lookup)
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
            if (od_day_slots) {
                MATHFP_TRY(validate_od_day_production_batch_invariants(
                      batch
                    , stats
                    , make_storage_diagnostics()
                ));
            }

            return slot_results;
        }

    }  // namespace
    namespace {

        [[nodiscard]] std::size_t search_slot_connection_count(
            std::span<const SearchSlotResult> results
        ) noexcept {
            std::size_t total = 0;
            for (const auto& result : results) {
                total += result.connection_count;
            }
            return total;
        }

        [[nodiscard]] std::size_t search_batch_worker_count(
              std::size_t batch_count
            , const SearchExecutionConfig& config
        ) noexcept {
            if (batch_count == 0u || config.max_parallel_batches == 0u) {
                return 0u;
            }
            const auto hardware = std::max(
                  1u
                , std::thread::hardware_concurrency()
            );
            auto worker_count = std::min(
                  batch_count
                , std::min<std::size_t>(
                      static_cast<std::size_t>(hardware)
                    , config.max_parallel_batches
                  )
            );
            if (config.max_parallel_memory_mb.has_value()
                && config.estimated_memory_mb_per_parallel_batch > 0u) {
                const auto memory_limited_workers = std::max<std::size_t>(
                      1u
                    , *config.max_parallel_memory_mb
                        / config.estimated_memory_mb_per_parallel_batch
                );
                worker_count = std::min(worker_count, memory_limited_workers);
            }
            return worker_count;
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

        [[nodiscard]] OriginDaySearchResult materialize_origin_day_search_result(
              ZoneId                        origin
            , std::vector<SearchSlotResult> slot_results
        ) {
            OriginDaySearchResult result{
                  .origin       = origin
                , .pair_results = {}
            };
            result.pair_results.reserve(slot_results.size());
            for (auto& slot_result : slot_results) {
                if (slot_result.slot.kind != SearchProjectionSlotKind::OdDayPair) {
                    continue;
                }
                result.pair_results.push_back(
                    OdDayPairResult{
                          .origin      = slot_result.slot.origin
                        , .destination = slot_result.slot.destination
                        , .alternatives = std::move(slot_result.day_path_alternatives)
                    }
                );
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
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
        const BranchAndBoundSearchRequest& request
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        const auto& network                       = request.network;
        const auto  tasks                         = request.tasks;
        const auto& execution                     = request.execution;
        const auto& params                        = request.params;
        const auto& search_cost                   = request.search_cost;
        const auto& choice_config                 = request.choice_config;
        const auto& assignment_period             = request.assignment_period;
        const auto& admissibility_config          = request.admissibility_config;
        const auto& complete_connection_dominance = request.complete_connection_dominance;
        const auto  diagnostics                   = request.diagnostics;

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
            request.pruning_execution.has_value()
                ? request.pruning_execution->get()
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
        log(
            fmt::format(
                  "timed/task contour: status=diagnostic_only formulation={} diagnostic_mode={} carrier=raw_timed_branch_trace result_projection={} demand_intervals_in_search={} od_day_production_separate=yes"
                , to_string(execution.config.formulation)
                , execution.config.diagnostic_mode ? "true" : "false"
                , to_string(execution.config.result_projection)
                , execution_mode == SearchExecutionMode::IntervalLocal
                    ? "interval_local"
                    : "origin_period_domain"
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
            if (!execution.time_domain_execution.has_value()) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin-period search requires SearchTimeDomainExecution")
                );
            }
            const auto& time_domain_execution =
                execution.time_domain_execution->get();
            mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
                execution.config.origin_scope == SearchOriginScope::DeclaredZones
                    ? build_declared_origin_period_search_tree_jobs(
                          execution.declared_zones
                        , tasks
                        , time_domain_execution
                        , execution.config.destination_scope
                      )
                    : build_origin_period_search_tree_jobs(
                          tasks
                        , time_domain_execution
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
        const BranchAndBoundSearchRequest& request
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        const auto& network                       = request.network;
        const auto  tasks                         = request.tasks;
        const auto& execution                     = request.execution;
        const auto& params                        = request.params;
        const auto& search_cost                   = request.search_cost;
        const auto& choice_config                 = request.choice_config;
        const auto& assignment_period             = request.assignment_period;
        const auto& admissibility_config          = request.admissibility_config;
        const auto& complete_connection_dominance = request.complete_connection_dominance;
        const auto  diagnostics                   = request.diagnostics;

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
        if (!execution.time_domain_execution.has_value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("all-zone origin-period search requires SearchTimeDomainExecution")
            );
        }
        const auto& time_domain_execution =
            execution.time_domain_execution->get();

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
            request.pruning_execution.has_value()
                ? request.pruning_execution->get()
                : default_pruning_execution;

        mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
            execution.config.origin_scope == SearchOriginScope::DeclaredZones
                ? build_declared_origin_period_search_tree_jobs(
                      execution.declared_zones
                    , tasks
                    , time_domain_execution
                    , execution.config.destination_scope
                  )
                : build_origin_period_search_tree_jobs(
                      tasks
                    , time_domain_execution
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

        const auto worker_count = search_batch_worker_count(
              batches.size()
            , execution.config
        );
        log(
            fmt::format(
                  "all-zone search parallel execution: workers={} batches={} max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={} fast_fail=enabled"
                , worker_count
                , batches.size()
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );

        std::atomic<std::size_t> next_batch{ 0u };
        std::atomic<std::size_t> completed_batches{ 0u };
        std::atomic<std::size_t> cancelled_batches{ 0u };
        std::atomic<std::size_t> found_connections{ 0u };
        SearchCancellationToken cancellation;
        CountOnlyAllZoneSearchResultSink result_sink;
        std::vector<std::future<mathfp::Expected<mathfp::Unit>>> workers;
        workers.reserve(worker_count);

        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.push_back(
                std::async(
                      std::launch::async
                    , [&, worker]() -> mathfp::Expected<mathfp::Unit> {
                          for (;;) {
                              if (search_cancelled(&cancellation)) {
                                  return mathfp::kUnit;
                              }
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

                              auto results_result = search_batch_connections(
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
                                      , nullptr
                                      , &cancellation
                                  );
                              if (!results_result) {
                                  request_search_cancellation(&cancellation);
                                  return mathfp::unexpected(std::move(results_result.error()));
                              }
                              if (search_cancelled(&cancellation)) {
                                  cancelled_batches.fetch_add(1u, std::memory_order_relaxed);
                                  return mathfp::kUnit;
                              }
                              auto results = std::move(*results_result);
                              found_connections.fetch_add(
                                    search_slot_connection_count(results)
                                  , std::memory_order_relaxed
                              );
                              auto sink_result = result_sink.accept(std::move(results));
                              if (!sink_result) {
                                  request_search_cancellation(&cancellation);
                                  return mathfp::unexpected(std::move(sink_result.error()));
                              }
                              completed_batches.fetch_add(1u, std::memory_order_relaxed);
                          }
                      }
                )
            );
        }

        mathfp::Expected<mathfp::Unit> first_worker_error = mathfp::kUnit;
        for (auto& worker : workers) {
            auto worker_result = worker.get();
            if (!worker_result && first_worker_error) {
                request_search_cancellation(&cancellation);
                first_worker_error = mathfp::unexpected(
                    std::move(worker_result.error())
                );
            }
        }
        if (cancelled_batches.load(std::memory_order_relaxed) != 0u) {
            log(
                fmt::format(
                      "all-zone search fast-fail cancellation: cancelled_batches={}"
                    , cancelled_batches.load(std::memory_order_relaxed)
                )
                , LogLevel::Warning
            );
        }
        MATHFP_TRY(std::move(first_worker_error));

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

    mathfp::Expected<mathfp::Unit> search_od_day_paths_by_origin_branch_and_bound(
        OdDayPathOriginSearchRequest request
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;
        using timetable::infra::progress::status;

        auto        origin_sink                   = std::move(request.origin_sink);
        const auto& search_request                = request.search;
        const auto& network                       = search_request.network;
        const auto  tasks                         = search_request.tasks;
        const auto& execution                     = search_request.execution;
        const auto& params                        = search_request.params;
        const auto& search_cost                   = search_request.search_cost;
        const auto& choice_config                 = search_request.choice_config;
        const auto& assignment_period             = search_request.assignment_period;
        const auto& admissibility_config          = search_request.admissibility_config;
        const auto& complete_connection_dominance =
            search_request.complete_connection_dominance;
        const auto  diagnostics                   = search_request.diagnostics;

        MATHFP_TRY(validate_search_execution_projection_contract(execution.config));
        if (execution.config.result_projection != SearchResultProjection::OdDayPairs) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OdDayPathSearchResult search requires OdDayPairs result projection")
                    .ctx("result_projection", std::string(to_string(execution.config.result_projection)))
            );
        }
        if (execution.config.mode != SearchExecutionMode::OriginPeriod) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search requires origin-period execution")
                    .ctx("execution_mode", std::string(to_string(execution.config.mode)))
            );
        }
        if (search_cost.mode != SearchCostMode::BaseOnly) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search currently supports only base search cost")
                    .ctx("search_cost_mode", std::string(to_string(search_cost.mode)))
            );
        }
        if (!execution.time_domain_execution.has_value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day origin-period search requires SearchTimeDomainExecution")
            );
        }
        const auto& time_domain_execution =
            execution.time_domain_execution->get();
        if (!origin_sink) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day by-origin search requires a result sink")
            );
        }
        if (execution.config.partial_retention_scope != SearchPartialRetentionScope::TreeGlobal) {
            return mathfp::unexpected(
                mathfp::invalid_arg("OD-day search requires tree_global partial retention")
                    .ctx("partial_retention_scope", std::string(to_string(execution.config.partial_retention_scope)))
            );
        }

        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));
        MATHFP_TRY(validate_complete_connection_dominance_config(
            complete_connection_dominance
        ));
        MATHFP_TRY(validate_search_cost_context(search_cost));
        const auto od_day_contract = make_od_day_path_search_contract(
            diagnostics.declared_zone_count
        );
        if (!satisfies_od_day_path_search_contract(od_day_contract)) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day search production contract is not satisfied")
                    .ctx(
                          "declared_origin_count"
                        , static_cast<std::int64_t>(od_day_contract.declared_origin_count)
                    )
            );
        }

        both("search: OD-day branch-and-bound");
        const auto day_path_retention_config = DayPathRetentionConfig{};
        const auto day_path_alternative_limit =
            day_path_retention_config.max_alternatives_per_od.has_value()
                ? std::to_string(*day_path_retention_config.max_alternatives_per_od)
                : std::string("unbounded");
        const auto day_path_support_limit =
            day_path_retention_config.max_supports_per_path.has_value()
                ? std::to_string(*day_path_retention_config.max_supports_per_path)
                : std::string("unbounded");
        log(
            fmt::format(
                  "OD-day projection contract: paper=connection_tree partial_retention_scope={} tree_label=network_node_c_y c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink support=connection_segment_witness dominance=dep_arr_imp_nt tolerance=node_local production_carrier=compact_connection_segment_prefix path_identity=compact_prefix supply_graph=preprocessed_connection_segment_index frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_contract=single_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available late_guard=assert_only transfer_walk=first_class_segment composite_transfer_walk=disabled_in_production label_representatives=unbounded tree_bounds=c_y_before_day_path_sink suffix_bound=disabled_for_od_day completed_connection_projection=immediate_day_path_sink od_alternative_retention=production_slots_only signature=route_stop_line_pattern day_path_retention_policy={} max_alternatives_per_od={} max_supports_per_path={} computation_contract={}"
                , to_string(execution.config.partial_retention_scope)
                , day_path_retention_limit_policy_name(
                      day_path_retention_config.limit_policy
                  )
                , day_path_alternative_limit
                , day_path_support_limit
                , to_log_token(OdDaySearchComputationContract::PaperConnectionSegmentTree)
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
            search_request.pruning_execution.has_value()
                ? search_request.pruning_execution->get()
                : default_pruning_execution;

        mathfp::Expected<std::vector<SearchTreeJob>> tree_jobs_result =
            execution.config.origin_scope == SearchOriginScope::DeclaredZones
                ? build_declared_origin_period_search_tree_jobs(
                      execution.declared_zones
                    , tasks
                    , time_domain_execution
                    , execution.config.destination_scope
                  )
                : build_origin_period_search_tree_jobs(
                      tasks
                    , time_domain_execution
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

        /*
         * The paper OD-day production contour does not use suffix reachability
         * masks as a branch filter. Keep an empty graph only to satisfy the
         * shared batch-search signature; timed/diagnostic contours still build
         * and use residual reachability in their own entry points.
         */
        const ResidualReverseGraph residual_reverse_graph{};
        const auto batch_execution_diagnostics = summarize_search_batches(batches);
        const auto expected_tree_count = expected_search_tree_count(
              execution.config.origin_scope
            , diagnostics.declared_zone_count
            , tasks
        );
        if (tree_jobs.size() != expected_tree_count) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day search must build exactly one tree per declared origin")
                    .ctx("tree_count", static_cast<std::int64_t>(tree_jobs.size()))
                    .ctx("expected_tree_count", static_cast<std::int64_t>(expected_tree_count))
                    .ctx("declared_zone_count", static_cast<std::int64_t>(diagnostics.declared_zone_count))
                    .ctx("declared_zone_request_count", static_cast<std::int64_t>(execution.declared_zones.size()))
            );
        }
        if (execution.config.origin_scope == SearchOriginScope::DeclaredZones
            && tree_jobs.size() != execution.declared_zones.size()) {
            return mathfp::unexpected(
                mathfp::internal_error("OD-day declared-origin tree count disagrees with declared zone support")
                    .ctx("tree_count", static_cast<std::int64_t>(tree_jobs.size()))
                    .ctx("declared_zone_request_count", static_cast<std::int64_t>(execution.declared_zones.size()))
            );
        }
        MATHFP_TRY(validate_od_day_production_batches(
              batches
            , expected_tree_count
            , execution.config.destination_scope
            , execution.config.destination_scope == SearchDestinationScope::DeclaredZones
                ? execution.declared_zones.size()
                : batch_execution_diagnostics.max_completion_targets_per_tree
        ));
        log(
            fmt::format(
                  "OD-day computational profile: contour=paper_branch_and_bound production_carrier=compact_connection_segment_prefix branch_projection_state=none reachability_prefilter=disabled_not_built reachability_masks=disabled supply_graph=preprocessed_connection_segment_index frontier=connection_segment_level_queues frontier_sync=label_registry_with_compaction successor_generation=single_paper_connection_segment_before_visitor temporal_suitability=timed_window_walk_always_available transfer_walk_successor=first_class_connection_segment composite_transfer_walk=disabled_in_production walk_successor_lookup=lazy_phase_specific walk_indices=access_transfer_egress tree_label_scope=network_node_c_y c_y_key=physical_y c_y_carrier=physical_node_only c_y_applies_to=all_connection_segments_before_sink dominance=dep_arr_imp_nt tolerance=node_local path_identity=compact_prefix od_signature=route_stop_line_pattern structural_day_contour=diagnostics_only trees={} destinations={} time_horizon=service_day result=post_layer_day_path_support_sets split_contract=paper_connection_split split_interval_admissibility=all_interval_admissible_timed_supports single_best_support=disabled split_load=lazy_support_envelope primary_load=elementary_segment_loads max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={}"
                , tree_jobs.size()
                , execution.config.destination_scope == SearchDestinationScope::DeclaredZones
                    ? execution.declared_zones.size()
                    : batch_execution_diagnostics.max_completion_targets_per_tree
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day paper connection segment tree: connection_segments={} route_segments={} timed_buckets={} boarding_stop_buckets={} access_walks={} transfer_walks={} egress_walks={}"
                , network.connection_segments.size()
                , network.route_segments.size()
                , network.connection_index.timed_buckets.size()
                , network.connection_index.boarding_stop_buckets.size()
                , network.connection_index.access_walk_order.size()
                , network.connection_index.transfer_walk_order.size()
                , network.connection_index.egress_walk_order.size()
            )
            , LogLevel::Info
        );
        log(
            fmt::format(
                  "OD-day search diagnostics: paper_clauses=successor_then_c_y_then_sink c_y_scope=node_local destination_in_expansion=no demand_intervals_in_search=no raw_complete_retention=no trees={} expected_trees={} tree_count_delta={} batches={} targets={} projection_slots={} partial_retention_scope={} phase_invariant_validation={} result_sink=origin"
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
        log(
            fmt::format(
                  "OD-day paper compliance: paper_reference=connection_segment_tree one_tree_per_origin={} trees={} expected_trees={} service_period=day demand_intervals_in_tree=no destination_in_expansion=no successor_order=lookup_then_feasibility_then_c_y_then_enqueue c_y_scope=node_local c_y_key=physical_y c_y_applies_to=all_connection_segments day_path_layer=post_layer production_result=day_path_support_sets raw_completed_connections_retained=no"
                , tree_jobs.size() == expected_tree_count ? "true" : "false"
                , tree_jobs.size()
                , expected_tree_count
            )
            , LogLevel::Info
        );

        const auto worker_count = search_batch_worker_count(
              batches.size()
            , execution.config
        );
        log(
            fmt::format(
                  "OD-day search parallel execution: workers={} batches={} max_parallel_batches={} max_parallel_memory_mb={} estimated_memory_mb_per_parallel_batch={} label_representatives=unbounded merge=origin_sink_associative fast_fail=enabled"
                , worker_count
                , batches.size()
                , execution.config.max_parallel_batches
                , format_optional_size_limit(execution.config.max_parallel_memory_mb)
                , execution.config.estimated_memory_mb_per_parallel_batch
            )
            , LogLevel::Info
        );

        std::atomic<std::size_t> next_batch{ 0u };
        std::atomic<std::size_t> completed_batches{ 0u };
        std::atomic<std::size_t> cancelled_batches{ 0u };
        std::atomic<std::size_t> pair_count{ 0u };
        std::atomic<std::size_t> day_path_alternative_count{ 0u };
        std::atomic<std::size_t> empty_pair_count{ 0u };
        SearchCancellationToken cancellation;
        std::mutex origin_sink_mutex;
        std::vector<std::future<mathfp::Expected<mathfp::Unit>>> workers;
        workers.reserve(worker_count);

        for (std::size_t worker = 0; worker < worker_count; ++worker) {
            workers.push_back(
                std::async(
                      std::launch::async
                    , [&, worker]() -> mathfp::Expected<mathfp::Unit> {
                          for (;;) {
                              if (search_cancelled(&cancellation)) {
                                  return mathfp::kUnit;
                              }
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
                                            "OD-day search: worker={} origin batch {}/{} origin={} targets={} projection_slots={} completed={} total_found={}"
                                          , worker
                                          , i + 1
                                          , batches.size()
                                          , batch.key.origin.get()
                                          , batch.completion_targets.size()
                                          , batch.projection_slots.size()
                                          , completed_batches.load(std::memory_order_relaxed)
                                          , day_path_alternative_count.load(std::memory_order_relaxed)
                                      )
                                  );
                              }

                              auto slot_results_result = search_batch_connections(
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
                                      , nullptr
                                      , &cancellation
                                  );
                              if (!slot_results_result) {
                                  request_search_cancellation(&cancellation);
                                  return mathfp::unexpected(std::move(slot_results_result.error()));
                              }
                              if (search_cancelled(&cancellation)) {
                                  cancelled_batches.fetch_add(1u, std::memory_order_relaxed);
                                  return mathfp::kUnit;
                              }
                              auto slot_results = std::move(*slot_results_result);
                              auto origin_result = materialize_origin_day_search_result(
                                    batch.key.origin
                                  , std::move(slot_results)
                              );
                              auto origin_alternatives = std::size_t{ 0u };
                              auto origin_empty_pairs = std::size_t{ 0u };
                              for (const auto& pair_result : origin_result.pair_results) {
                                  origin_alternatives += pair_result.alternatives.size();
                                  if (pair_result.alternatives.empty()) {
                                      ++origin_empty_pairs;
                                  }
                              }
                              day_path_alternative_count.fetch_add(
                                    origin_alternatives
                                  , std::memory_order_relaxed
                              );
                              empty_pair_count.fetch_add(
                                    origin_empty_pairs
                                  , std::memory_order_relaxed
                              );
                              pair_count.fetch_add(
                                    origin_result.pair_results.size()
                                  , std::memory_order_relaxed
                              );
                              {
                                  std::scoped_lock lock(origin_sink_mutex);
                                  auto sink_result = origin_sink(std::move(origin_result));
                                  if (!sink_result) {
                                      request_search_cancellation(&cancellation);
                                      return mathfp::unexpected(std::move(sink_result.error()));
                                  }
                              }
                              completed_batches.fetch_add(1u, std::memory_order_relaxed);
                          }
                      }
                )
            );
        }

        mathfp::Expected<mathfp::Unit> first_worker_error = mathfp::kUnit;
        for (auto& worker : workers) {
            auto worker_result = worker.get();
            if (!worker_result && first_worker_error) {
                request_search_cancellation(&cancellation);
                first_worker_error = mathfp::unexpected(
                    std::move(worker_result.error())
                );
            }
        }
        if (cancelled_batches.load(std::memory_order_relaxed) != 0u) {
            log(
                fmt::format(
                      "OD-day search fast-fail cancellation: cancelled_batches={}"
                    , cancelled_batches.load(std::memory_order_relaxed)
                )
                , LogLevel::Warning
            );
        }
        MATHFP_TRY(std::move(first_worker_error));
        log(
            fmt::format(
                  "OD-day by-origin search result: carrier=paper_connection_tree result=day_path_post_layer origins={:>8} pairs={:>8} empty_pairs={:>8} expected_trees={:>8} day_path_alternatives={:>8} raw_complete_connections={:>8}"
                , batches.size()
                , pair_count.load(std::memory_order_relaxed)
                , empty_pair_count.load(std::memory_order_relaxed)
                , expected_tree_count
                , day_path_alternative_count.load(std::memory_order_relaxed)
                , std::size_t{ 0u }
            )
            , LogLevel::Info
        );
        both("search: OD-day branch-and-bound done");
        return mathfp::kUnit;
    }

}  // namespace timetable::domain::assignment::runtime
