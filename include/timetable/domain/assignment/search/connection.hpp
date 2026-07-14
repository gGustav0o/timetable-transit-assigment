#pragma once

#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/connection.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Strict search-stage connection alternative.
     *
     * The stored fact is the canonical OD-bound connection. Metrics, traces
     * and behavioral evaluations are derived projections.
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

}  // namespace timetable::domain::assignment
