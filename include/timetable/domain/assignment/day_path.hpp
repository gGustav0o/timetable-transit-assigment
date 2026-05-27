#pragma once

#include <compare>
#include <cstddef>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Incremental tree-label prefix used by OD-day search.
     *
     * The prefix is a search label, not a finalized OD alternative. It is
     * deliberately clock-free and is converted to the coarser production
     * DayPathSignature only when a destination is reached.
     */
    struct DayPathPrefix final {
        ZoneId                  origin{};
        std::vector<DayPathLeg> legs{};

        auto operator<=>(const DayPathPrefix&) const = default;
    };

    /**
     * @brief Behavioral metrics of a day-level path alternative.
     *
     * The path is structural. Behavioral evaluation is derived from compact
     * timed support descriptors retained under that path; representative metrics
     * are only the canonical path-quality projection used by choice tolerances.
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

    struct DayPathRetentionConfig final {
        std::size_t max_alternatives_per_od{ 256u };
        std::size_t max_supports_per_path{ 16u };
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

    [[nodiscard]] DayPathLeg production_day_path_leg(
        DayPathLeg leg
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

    [[nodiscard]] DayPathSignature make_day_path_signature_from_tree_label(
          DayPathPrefix prefix
        , ZoneId        destination
    );

    [[nodiscard]] DayPathSignature day_path_signature_of(
        const SearchConnection& connection
    );

    [[nodiscard]] const DayPathSignature& day_path_signature_of(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] const DayPathTimedSupport& day_path_support_of(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] const SearchConnection& day_path_representative_connection(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] std::span<const DayPathSupportDescriptor> day_path_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] std::span<const DayPathSupportDescriptor> day_path_split_support_descriptors(
        const DayPathAlternative& alternative
    ) noexcept;

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_day_path_alternative(
          const DayPathAlternative& alternative
        , std::size_t               alternative_index
    );

    [[nodiscard]] mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&       retention
        , SearchConnection        connection
        , const SearchCostContext& search_cost
        , IntervalId              interval
    );

    [[nodiscard]] mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , SearchConnection              connection
        , const SearchCostContext&      search_cost
        , IntervalId                    interval
        , const DayPathRetentionConfig& config
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
