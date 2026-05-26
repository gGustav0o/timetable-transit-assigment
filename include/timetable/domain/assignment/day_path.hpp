#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief One structural leg of a day-level OD path.
     *
     * A DayPathLeg deliberately excludes concrete connection segment ids, trip
     * ids and clock times. It keeps only the supply structure that determines
     * the path pattern used for day-level assignment.
     */
    struct DayPathLeg final {
        ConnectionLegKind                kind{};
        std::optional<RouteSegmentId>    route_segment{};
        EndpointKey                      physical_from{};
        EndpointKey                      physical_to{};
        std::optional<StopOccurrenceKey> occurrence_from{};
        std::optional<StopOccurrenceKey> occurrence_to{};
        std::optional<LineId>            line{};
        std::optional<RouteId>           route{};

        auto operator<=>(const DayPathLeg&) const = default;
    };

    /**
     * @brief Canonical day-level path identity for one OD pair.
     *
     * SearchConnection is a time-realized timetable connection. DayPathSignature
     * is the corresponding all-day structural path pattern between zones.
     */
    struct DayPathSignature final {
        ZoneId                  origin{};
        ZoneId                  destination{};
        std::vector<DayPathLeg> legs{};

        auto operator<=>(const DayPathSignature&) const = default;
    };

    struct DayPathAlternative final {
        DayPathSignature         signature{};
        SearchConnection         representative;
        CompleteConnectionMetrics representative_metrics{};
        ConnectionMetrics         representative_connection_metrics{};
        std::size_t              timed_connection_count{};
    };

    /**
     * @brief Behavioral metrics of a day-level path alternative.
     *
     * The path is structural, while behavioral evaluation still needs one
     * concrete timetable support. representative_connection_metrics are derived
     * from that support; representative_metrics are the matching search-cost
     * metrics used by complete-connection dominance and choice tolerances.
     */
    struct DayPathMetrics final {
        CompleteConnectionMetrics complete{};
        ConnectionMetrics         representative{};
        std::size_t               timed_connection_count{};
    };

    struct DayPathRetentionDecision final {
        bool        inserted_path{};
        bool        replaced_representative{};
        std::size_t timed_connection_count{};
    };

    struct DayPathRetention final {
        std::vector<DayPathAlternative> alternatives{};
    };

    [[nodiscard]] DayPathSignature day_path_signature_of(
        const SearchConnection& connection
    );

    [[nodiscard]] mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&       retention
        , SearchConnection        connection
        , const SearchCostContext& search_cost
        , IntervalId              interval
    );

    [[nodiscard]] mathfp::Expected<DayPathAlternative> make_day_path_alternative(
          SearchConnection        connection
        , const SearchCostContext& search_cost
        , IntervalId              interval
    );

    [[nodiscard]] DayPathMetrics day_path_metrics_of(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] std::vector<SearchConnection> finalize_day_path_representatives(
        DayPathRetention retention
    );

    [[nodiscard]] std::vector<SearchConnection> day_path_representative_connections(
        std::span<const DayPathAlternative> alternatives
    );

    [[nodiscard]] mathfp::Expected<std::vector<SearchConnection>>
    retain_day_path_representative_connections(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    );

    [[nodiscard]] mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
    );

}  // namespace timetable::domain::assignment
