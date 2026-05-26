#pragma once

#include <compare>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Incremental structural path prefix used by OD-day search.
     *
     * The prefix is deliberately clock-free: it grows while the timetable
     * support is explored, but it does not contain trip ids, connection segment
     * ids or waiting legs. This lets OD-day search reason about the day-level
     * path before a complete timed support is materialized.
     */
    struct DayPathPrefix final {
        ZoneId                  origin{};
        std::vector<DayPathLeg> legs{};

        auto operator<=>(const DayPathPrefix&) const = default;
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
        std::map<DayPathSignature, DayPathAlternative> alternatives_by_signature{};
    };

    [[nodiscard]] bool day_path_retention_empty(
        const DayPathRetention& retention
    ) noexcept;

    [[nodiscard]] std::size_t day_path_retention_size(
        const DayPathRetention& retention
    ) noexcept;

    [[nodiscard]] DayPathLeg day_path_leg_of(
        const ConnectionLeg& leg
    ) noexcept;

    [[nodiscard]] DayPathPrefix make_day_path_prefix(
        ZoneId origin
    );

    [[nodiscard]] DayPathPrefix append_day_path_leg(
          DayPathPrefix prefix
        , DayPathLeg    leg
    );

    [[nodiscard]] DayPathSignature complete_day_path_signature(
          DayPathPrefix prefix
        , ZoneId        destination
    );

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

    [[nodiscard]] std::vector<DayPathAlternative> finalize_day_path_alternatives(
        DayPathRetention retention
    );

    [[nodiscard]] CompleteConnectionMetricSummary summarize_day_path_metrics(
        const DayPathRetention& retention
    ) noexcept;

    [[nodiscard]] std::vector<DayPathAlternative> finalize_day_path_alternatives(
          DayPathRetention        retention
        , const ChoiceTolerances& tolerances
        , ChoiceRolloutStage      rollout_stage
    );

    [[nodiscard]] std::vector<SearchConnection> finalize_day_path_representatives(
          DayPathRetention        retention
        , const ChoiceTolerances& tolerances
        , ChoiceRolloutStage      rollout_stage
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

    [[nodiscard]] mathfp::Expected<std::vector<DayPathAlternative>>
    retain_day_path_alternatives(
          std::vector<SearchConnection> connections
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const ChoiceTolerances&       tolerances
        , ChoiceRolloutStage            rollout_stage
    );

}  // namespace timetable::domain::assignment
