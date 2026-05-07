#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/types/strong_type.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/assignment_period.hpp"
#include "timetable/domain/assignment/choice/choice_config.hpp"
#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include <timetable/domain/segments.hpp>

namespace timetable::domain::assignment {

    struct CompleteConnectionDominanceConfig;
    struct SearchCostContext;

    /**
     * @brief Search execution request passed from pipeline orchestration.
     *
     * config chooses the sharing contract and scopes for search trees.
     * time_domain_execution is intentionally optional and consumed only by
     * OriginPeriod: IntervalLocal remains a separate fast batching path over
     * SearchTask::departure_domain.
     */
    struct SearchExecutionRequest final {
        SearchExecutionConfig            config{};
        const SearchTimeDomainExecution* time_domain_execution{};
        std::span<const Zone>            declared_zones{};
    };

    struct SearchTaskRefTag {};

    using SearchTaskRef = mathfp::StrongType<
          std::int64_t
        , SearchTaskRefTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    /**
     * @brief OD-time demand task projected onto timetable connection search.
     *
     * A task asks for alternatives for exactly one origin-destination demand
     * interval. It is the assignment unit: search algorithms may share tree
     * work between tasks, but the public result must keep alternatives attached
     * to this OD-time unit.
     */
    struct SearchTask final {
        SearchTaskRef   index{};
        ZoneId          origin{};
        ZoneId          destination{};
        TimeInterval    interval{};
        SearchTimeDomain departure_domain{};
    };

    struct SearchTreeJobRefTag {};

    using SearchTreeJobRef = mathfp::StrongType<
          std::int64_t
        , SearchTreeJobRefTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    struct SearchCompletionTargetRefTag {};

    using SearchCompletionTargetRef = mathfp::StrongType<
          std::int64_t
        , SearchCompletionTargetRefTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    /**
     * @brief Destination target at which an origin-wide search may complete.
     *
     * This is intentionally not an OD-time demand task: a completion target
     * belongs to tree enumeration, while SearchTask remains the assignment and
     * split projection unit.
     */
    struct SearchCompletionTarget final {
        SearchCompletionTargetRef index{};
        ZoneId                    destination{};
    };

    /**
     * @brief Origin-keyed search-tree job independent of OD-time task storage.
     *
     * This type is the bridge toward origin-period execution: one tree job owns
     * the origin key and the first-boarding search domain used by that tree,
     * while demand tasks remain separate projections attached by reference.
     */
    struct SearchTreeJob final {
        SearchTreeJobRef                     index{};
        ZoneId                               origin{};
        SearchTimeDomain                     departure_domain{};
        std::vector<SearchCompletionTarget>  completion_targets{};
        std::vector<SearchTaskRef>           projection_tasks{};
    };

    /**
     * @brief Strict search-stage connection alternative.
     *
     * The only stored fact is the canonical OD-bound connection. Metrics,
     * segment-id traces and behavioral evaluations are derived projections.
     */
    struct SearchConnection final {
        SearchConnection() = delete;
        SearchConnection(const SearchConnection&) = default;
        SearchConnection(SearchConnection&&) noexcept = default;
        SearchConnection& operator=(const SearchConnection&) = default;
        SearchConnection& operator=(SearchConnection&&) noexcept = default;

    private:
        explicit SearchConnection(Connection connection);

        Connection connection_;

        friend mathfp::Expected<SearchConnection> make_search_connection(
            Connection connection
        );
        friend mathfp::Expected<SearchConnection> make_search_connection(
              ZoneId          origin
            , ZoneId          destination
            , ConnectionTrace trace
        );
        friend const Connection& canonical_connection(
            const SearchConnection& connection
        ) noexcept;
    };

    struct SearchTaskResult final {
        SearchTask                    task{};
        // Complete alternatives retained for this task after exact dominance
        // and the task-final tolerance pass requested by ChoiceConfig.
        std::vector<SearchConnection> connections{};
    };

    struct ConnectionSearchResult final {
        std::vector<SearchTaskResult> task_results{};
    };

    struct AllZoneTargetResult final {
        ZoneId                        origin{};
        ZoneId                        destination{};
        // Count is authoritative for streaming/count-only all-zone sinks; the
        // connection vector is optional materialized payload.
        std::size_t                   connection_count{};
        std::vector<SearchConnection> connections{};
    };

    struct AllZoneTreeResult final {
        ZoneId                           origin{};
        std::vector<AllZoneTargetResult> target_results{};
    };

    struct AllZoneConnectionSearchResult final {
        std::vector<AllZoneTreeResult> tree_results{};
    };

    /**
     * @brief Runtime diagnostics context for search logging.
     *
     * This is intentionally separate from SearchCostContext: it describes the
     * outer execution run, not the mathematical cost function optimized by
     * branch-and-bound.
     */
    struct SearchDiagnosticsContext final {
        std::int32_t capacity_iteration{};
        std::size_t  declared_zone_count{};
        bool         validate_phase_invariants{};
    };

    mathfp::Expected<std::vector<SearchTask>> build_search_tasks(
        const InputModel& input
    );

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask> tasks
        , const SearchTimeDomain&     period_domain
    );

    mathfp::Expected<std::vector<SearchTreeJob>> build_origin_period_search_tree_jobs(
          std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
    );

    mathfp::Expected<std::vector<SearchTreeJob>> build_declared_origin_period_search_tree_jobs(
          std::span<const Zone>             declared_zones
        , std::span<const SearchTask>       tasks
        , const SearchTimeDomainExecution&  execution
        , SearchDestinationScope            destination_scope
    );

    [[nodiscard]] std::vector<const SearchConnection*> search_connection_ptrs(
        const ConnectionSearchResult& result
    );

    [[nodiscard]] std::size_t search_connection_count(
        const ConnectionSearchResult& result
    ) noexcept;

    [[nodiscard]] std::size_t search_connection_count(
        const AllZoneConnectionSearchResult& result
    ) noexcept;

    [[nodiscard]] std::size_t all_zone_target_connection_count(
        const AllZoneTargetResult& target
    ) noexcept;

    mathfp::Expected<SearchConnection> make_search_connection(
        Connection connection
    );

    mathfp::Expected<SearchConnection> make_search_connection(
          ZoneId          origin
        , ZoneId          destination
        , ConnectionTrace trace
    );

    [[nodiscard]] const Connection& canonical_connection(
        const SearchConnection& connection
    ) noexcept;

    [[nodiscard]] ZoneId origin_of(
        const SearchConnection& connection
    ) noexcept;

    [[nodiscard]] ZoneId destination_of(
        const SearchConnection& connection
    ) noexcept;

    [[nodiscard]] ConnectionMetrics metrics_of(
        const SearchConnection& connection
    );

    [[nodiscard]] Time departure_time_of(
        const SearchConnection& connection
    );

    [[nodiscard]] Time arrival_time_of(
        const SearchConnection& connection
    );

    [[nodiscard]] Time journey_time_of(
        const SearchConnection& connection
    );

    [[nodiscard]] Time transfer_time_of(
        const SearchConnection& connection
    );

    [[nodiscard]] TransferCount transfer_count_of(
        const SearchConnection& connection
    );

    [[nodiscard]] double fare_of(
        const SearchConnection& connection
    );

    [[nodiscard]] std::vector<ConnectionSegmentId> connection_segment_trace(
        const SearchConnection& connection
    );

    /**
     * @brief Enumerate feasible connections using timetable-based branch & bound.
     */
    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution = nullptr
        , SearchDiagnosticsContext diagnostics = {}
    );

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , SearchExecutionRequest             execution
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics = {}
    );

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , SearchExecutionMode               execution_mode
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution = nullptr
        , SearchDiagnosticsContext diagnostics = {}
    );

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , SearchExecutionMode               execution_mode
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics = {}
    );

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics = {}
    );

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , double                            fare_scale
        , const SearchParams&               params
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution = nullptr
        , SearchDiagnosticsContext diagnostics = {}
    );

    mathfp::Expected<ConnectionSearchResult> search_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , double                            fare_scale
        , const SearchParams&               params
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics = {}
    );

    mathfp::Expected<AllZoneConnectionSearchResult> search_all_zone_connections_branch_and_bound(
          const PreprocessedNetwork&        network
        , std::span<const SearchTask>        tasks
        , SearchExecutionRequest             execution
        , const SearchParams&               params
        , const SearchCostContext&          search_cost
        , const ChoiceConfig&                choice_config
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
        , const SearchPruningExecutionPlan* pruning_execution
        , const CompleteConnectionDominanceConfig& complete_connection_dominance
        , SearchDiagnosticsContext diagnostics = {}
    );

}  // namespace timetable::domain::assignment
