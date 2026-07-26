#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <boost/container/small_vector.hpp>
#include <boost/container_hash/hash.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/day_path.hpp"
#include "timetable/domain/assignment/search/model/branch.hpp"
#include "timetable/domain/assignment/search_pruning.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/endpoints.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment {

    using SearchNodeKey = SearchPruningStateKey;
    using NodeMetricSet = SearchPruningMetricSet;

    struct NodeConnectionSetKey final {
        EndpointKey physical{};

        bool operator==(const NodeConnectionSetKey&) const = default;
    };

    struct NodeConnectionSetKeyHash final {
        std::size_t operator()(const NodeConnectionSetKey& key) const noexcept {
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

    struct RetainedConnectionLabelRegistry final {
        std::vector<bool> active{};
        std::vector<std::optional<RetainedConnectionLabelId>> parent{};
    };

    [[nodiscard]] RetainedConnectionLabelId allocate_retained_connection_label(
          RetainedConnectionLabelRegistry&         registry
        , std::optional<RetainedConnectionLabelId> parent
    );

    void deactivate_retained_connection_label(
          RetainedConnectionLabelRegistry& registry
        , RetainedConnectionLabelId        label
    ) noexcept;

    [[nodiscard]] bool retained_connection_label_active(
          const RetainedConnectionLabelRegistry& registry
        , std::optional<RetainedConnectionLabelId> label
    ) noexcept;

    struct ConnectionSetCyEntry final {
        SearchPruningMetrics    metrics{};
        RetainedConnectionLabelId  label{};
    };

    using ConnectionSetCyEntryVector = boost::container::small_vector<
          ConnectionSetCyEntry
        , 1
    >;

    /**
     * @brief Connection-tree-level algebraic carrier C_y for one physical network node y.
     *
     * Invariants:
     * - entries are ordered by arrival time
     * - entries are exact-nondominated under the node-local relevance relation
     * - summary is the minimum summary over entries
     * - every retained metric has exactly one frontier label
     *
     * The label is implementation support for frontier liveness; it is stored
     * in the same entry as the metric so the invalid "metric/label cardinality
     * mismatch" state is not representable.
     */
    class ConnectionSetCy final {
    public:
        [[nodiscard]] const ConnectionSetCyEntryVector& entries() const noexcept {
            return entries_;
        }

        [[nodiscard]] const SearchPruningSummary& summary() const noexcept {
            return summary_;
        }

        [[nodiscard]] bool empty() const noexcept {
            return entries_.empty();
        }

        [[nodiscard]] std::size_t size() const noexcept {
            return entries_.size();
        }

    private:
        ConnectionSetCyEntryVector entries_{};
        SearchPruningSummary       summary_{};

        friend std::size_t remove_inactive_node_connection_metrics(
              ConnectionSetCy&                    set
            , const RetainedConnectionLabelRegistry& registry
        );
        friend void insert_node_connection_metrics(
              const SearchPruningExecutionPlan&      pruning_execution
            , ConnectionSetCy&                       set
            , SearchPruningMetrics                   metrics
            , RetainedConnectionLabelId                 label
            , std::vector<RetainedConnectionLabelId>&   removed_labels
        );
        friend bool node_connection_relevant(
              const ConnectionSetCy&       set
            , const ExactPruningPolicy&    policy
            , const SearchPruningMetrics&  candidate
        ) noexcept;
    };

    using NodeConnectionSetMap = boost::unordered_flat_map<
          NodeConnectionSetKey
        , ConnectionSetCy
        , NodeConnectionSetKeyHash
    >;

}  // namespace timetable::domain::assignment
