#pragma once

#include <cstdint>
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
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include <timetable/domain/segments.hpp>

namespace timetable::domain::assignment {

    struct CompleteConnectionDominanceConfig;
    struct SearchCostContext;

    struct SearchTaskRefTag {};

    using SearchTaskRef = mathfp::StrongType<
          std::int64_t
        , SearchTaskRefTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    /**
     * @brief Mathematical unit of timetable connection search.
     *
     * A task asks for feasible alternatives for exactly one
     * origin-destination demand interval under one departure-time domain.
     * Search algorithms may share implementation work between tasks, but the
     * public search result must keep alternatives attached to this unit.
     */
    struct SearchTask final {
        SearchTaskRef   index{};
        ZoneId          origin{};
        ZoneId          destination{};
        TimeInterval    interval{};
        SearchTimeDomain departure_domain{};
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

    mathfp::Expected<std::vector<SearchTask>> build_search_tasks(
          const InputModel&              input
        , const AssignmentPeriodConfig&  assignment_period
    );

    [[nodiscard]] std::vector<const SearchConnection*> search_connection_ptrs(
        const ConnectionSearchResult& result
    );

    [[nodiscard]] std::size_t search_connection_count(
        const ConnectionSearchResult& result
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
    );

}  // namespace timetable::domain::assignment
