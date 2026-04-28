#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/model.hpp"
#include "timetable/domain/params.hpp"
#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"
#include <timetable/domain/segments.hpp>

namespace timetable::domain::assignment {

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
        friend const Connection& canonical_connection(
            const SearchConnection& connection
        ) noexcept;
    };

    struct ConnectionSearchResult final {
        std::vector<SearchConnection> connections{};
    };

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
        , double                            fare_scale
        , const SearchParams&               params
        , const SearchPruningExecutionPlan* pruning_execution     = nullptr
        , const SearchTimeDomainExecution*  time_domain_execution = nullptr
    );

}  // namespace timetable::domain::assignment
