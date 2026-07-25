#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/od_day_path_result.hpp"
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
              std::size_t&       seed
            , const EndpointKey& endpoint
        ) noexcept {
            boost::hash_combine(seed, static_cast<std::uint8_t>(endpoint.kind));
            boost::hash_combine(seed, endpoint.id);
        }

        static void combine_occurrence(
              std::size_t&             seed
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
     * performance profile and may change the resulting assignment.
     * FailOnSaturation is a guarded bounded profile: hitting a configured cap
     * is reported as an error instead of silently changing the alternative set.
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

}  // namespace timetable::domain::assignment
