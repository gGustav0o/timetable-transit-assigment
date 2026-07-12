#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/search/search.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Incremental path identity carried by an OD-day search branch.
     *
     * The prefix is branch payload, not the dominance state of the OD-day
     * tree. It is deliberately clock-free and is converted to the coarser
     * production DayPathSignature only when a destination is reached.
     */
    struct DayPathPrefix final {
        ZoneId                  origin;
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

    struct DayPathLegHash final {
        static void combine_endpoint(
              std::size_t&      seed
            , const EndpointKey& endpoint
        ) noexcept {
            boost::hash_combine(seed, static_cast<std::uint8_t>(endpoint.kind));
            boost::hash_combine(seed, endpoint.id);
        }

        static void combine_occurrence(
              std::size_t&            seed
            , const StopOccurrenceKey& occurrence
        ) noexcept {
            boost::hash_combine(seed, occurrence.stop.get());
            boost::hash_combine(seed, occurrence.position.get());
        }

        std::size_t operator()(const DayPathLeg& leg) const noexcept {
            auto seed = std::size_t{ 0u };
            boost::hash_combine(seed, static_cast<std::uint8_t>(leg.kind));
            boost::hash_combine(seed, leg.route_segment.has_value());
            if (leg.route_segment.has_value()) {
                boost::hash_combine(seed, leg.route_segment->get());
            }
            combine_endpoint(seed, leg.physical_from);
            combine_endpoint(seed, leg.physical_to);
            boost::hash_combine(seed, leg.occurrence_from.has_value());
            if (leg.occurrence_from.has_value()) {
                combine_occurrence(seed, *leg.occurrence_from);
            }
            boost::hash_combine(seed, leg.occurrence_to.has_value());
            if (leg.occurrence_to.has_value()) {
                combine_occurrence(seed, *leg.occurrence_to);
            }
            boost::hash_combine(seed, leg.line.has_value());
            if (leg.line.has_value()) {
                boost::hash_combine(seed, leg.line->get());
            }
            boost::hash_combine(seed, leg.route.has_value());
            if (leg.route.has_value()) {
                boost::hash_combine(seed, leg.route->get());
            }
            return seed;
        }
    };

    struct DayPathSignatureHash final {
        std::size_t operator()(const DayPathSignature& signature) const noexcept {
            auto seed = std::size_t{ 0u };
            boost::hash_combine(seed, signature.origin.get());
            boost::hash_combine(seed, signature.destination.get());
            boost::hash_combine(seed, signature.legs.size());
            for (const auto& leg : signature.legs) {
                boost::hash_combine(seed, DayPathLegHash{}(leg));
            }
            return seed;
        }
    };

    using DayPathAlternativeMap = boost::unordered_flat_map<
          DayPathSignature
        , DayPathAlternative
        , DayPathSignatureHash
    >;

    struct DayPathRetention final {
        DayPathAlternativeMap alternatives_by_signature{};
    };

    /**
     * @brief Technical retention policy for OD-day alternatives.
     *
     * Unbounded is the truth-first production default: no structural path or
     * split support is removed because of a storage cap. BoundedDropWorst is a
     * performance profile and may change the resulting assignment. FailOnSaturation
     * is a guarded bounded profile: hitting a configured cap is reported as an
     * error instead of silently changing the alternative set.
     */
    enum class DayPathRetentionLimitPolicy : std::uint8_t {
          Unbounded
        , BoundedDropWorst
        , FailOnSaturation
    };

    struct DayPathRetentionConfig final {
        DayPathRetentionLimitPolicy limit_policy{
            DayPathRetentionLimitPolicy::Unbounded
        };
        std::optional<std::size_t> max_alternatives_per_od{};
        std::optional<std::size_t> max_supports_per_path{};
    };

    [[nodiscard]] constexpr const char* day_path_retention_limit_policy_name(
        DayPathRetentionLimitPolicy policy
    ) noexcept {
        switch (policy) {
            case DayPathRetentionLimitPolicy::Unbounded:
                return "unbounded";
            case DayPathRetentionLimitPolicy::BoundedDropWorst:
                return "bounded_drop_worst";
            case DayPathRetentionLimitPolicy::FailOnSaturation:
                return "fail_on_saturation";
        }
        return "unknown";
    }

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

    [[nodiscard]] mathfp::Expected<DayPathRetentionDecision> retain_day_path_alternative(
          DayPathRetention&             retention
        , DayPathSignature              signature
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
