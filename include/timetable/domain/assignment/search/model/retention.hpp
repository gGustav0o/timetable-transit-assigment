#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search_execution_config.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    using SearchNodeKey = SearchPruningStateKey;
    using NodeMetricSet = SearchPruningMetricSet;

    struct PaperConnectionNodeKey final {
        EndpointKey physical{};

        bool operator==(const PaperConnectionNodeKey&) const = default;
    };

    struct PaperConnectionNodeKeyHash final {
        std::size_t operator()(const PaperConnectionNodeKey& key) const noexcept {
            std::size_t seed = 23u;
            boost::hash_combine(seed, static_cast<std::uint8_t>(key.physical.kind));
            boost::hash_combine(seed, key.physical.id);
            return seed;
        }
    };

    struct SearchNodeKeyHash final {
        std::size_t operator()(const SearchNodeKey& key) const noexcept {
            std::size_t seed = 17u;
            boost::hash_combine(seed, static_cast<std::uint8_t>(key.physical.kind));
            boost::hash_combine(seed, key.physical.id);
            boost::hash_combine(seed, key.occurrence.has_value());
            if (key.occurrence.has_value()) {
                boost::hash_combine(seed, key.occurrence->stop.get());
                boost::hash_combine(seed, key.occurrence->position.get());
            }
            boost::hash_combine(seed, static_cast<std::uint8_t>(key.phase));
            boost::hash_combine(seed, key.transfer.last_trip.has_value());
            if (key.transfer.last_trip.has_value()) {
                boost::hash_combine(seed, key.transfer.last_trip->get());
            }
            boost::hash_combine(seed, key.transfer.last_line.has_value());
            if (key.transfer.last_line.has_value()) {
                boost::hash_combine(seed, key.transfer.last_line->get());
            }
            return seed;
        }
    };

    using NodeMetricMap = boost::unordered_flat_map<
          SearchNodeKey
        , NodeMetricSet
        , SearchNodeKeyHash
    >;

    /**
     * @brief Paper-level C_y container for one physical network node y.
     *
     * The paper stores known connections to y in arrival-sorted lists. That
     * matters for relevance: a known connection can dominate a candidate
     * only when its arrival is not later than the candidate arrival.
     */
    struct PaperNodeConnectionSet final {
        SearchPruningMetricVector metrics{};
        PaperConnectionLabelVector labels{};
        SearchPruningSummary       summary{};
    };

    struct PaperConnectionLabelRegistry final {
        std::vector<bool> active{};
        std::vector<std::optional<PaperConnectionLabelId>> parent{};
    };

    [[nodiscard]] PaperConnectionLabelId allocate_paper_connection_label(
          PaperConnectionLabelRegistry&         registry
        , std::optional<PaperConnectionLabelId> parent
    );

    void deactivate_paper_connection_label(
          PaperConnectionLabelRegistry& registry
        , PaperConnectionLabelId        label
    ) noexcept;

    [[nodiscard]] bool paper_connection_label_active(
          const PaperConnectionLabelRegistry& registry
        , std::optional<PaperConnectionLabelId> label
    ) noexcept;

    using PaperConnectionNodeMetricMap = boost::unordered_flat_map<
          PaperConnectionNodeKey
        , PaperNodeConnectionSet
        , PaperConnectionNodeKeyHash
    >;

    struct StructuralLabelState final {
        EndpointKey                       physical{};
        std::optional<StopOccurrenceKey>  current_occurrence{};
        SearchBranchPhase                 phase{ SearchBranchPhase::AtOrigin };

        bool operator==(const StructuralLabelState&) const = default;
    };

    struct OdDayLabelState final {
        StructuralLabelState   structural{};
        TimedSupportEnvelopeKey support{};

        bool operator==(const OdDayLabelState&) const = default;
    };

    struct StructuralLabelStateHash final {
        std::size_t operator()(const StructuralLabelState& key) const noexcept {
            std::size_t seed = 29u;
            boost::hash_combine(seed, static_cast<std::uint8_t>(key.physical.kind));
            boost::hash_combine(seed, key.physical.id);
            boost::hash_combine(seed, key.current_occurrence.has_value());
            if (key.current_occurrence.has_value()) {
                boost::hash_combine(seed, key.current_occurrence->stop.get());
                boost::hash_combine(seed, key.current_occurrence->position.get());
            }
            boost::hash_combine(seed, static_cast<std::uint8_t>(key.phase));
            return seed;
        }
    };

    struct OdDayLabelStateHash final {
        std::size_t operator()(const OdDayLabelState& key) const noexcept {
            std::size_t seed = StructuralLabelStateHash{}(key.structural);
            boost::hash_combine(seed, key.support.last_timed_occurrence.has_value());
            if (key.support.last_timed_occurrence.has_value()) {
                boost::hash_combine(seed, key.support.last_timed_occurrence->stop.get());
                boost::hash_combine(seed, key.support.last_timed_occurrence->position.get());
            }
            boost::hash_combine(seed, key.support.last_line.has_value());
            if (key.support.last_line.has_value()) {
                boost::hash_combine(seed, key.support.last_line->get());
            }
            return seed;
        }
    };

    struct OdDayLabelRetentionConfig final {
        std::size_t max_representatives_per_label{
            SearchExecutionConfig::kDefaultMaxOdDayLabelRepresentativesPerState
        };
    };

    struct OdDayLabelRepresentative final {
        SearchPruningMetrics metrics{};
        TimedSupportEnvelope support{};
    };

    struct OdDayLabelRepresentativeSet final {
        std::vector<OdDayLabelRepresentative> representatives{};
        SearchPruningMetricSet                summary_metrics{};
    };

    using OdDayLabelStateMap = boost::unordered_flat_map<
          OdDayLabelState
        , OdDayLabelRepresentativeSet
        , OdDayLabelStateHash
    >;

    [[nodiscard]] OdDayLabelRetentionConfig od_day_label_retention_config_of(
        const SearchExecutionConfig& config
    ) noexcept;

}  // namespace timetable::domain::assignment
