#pragma once

#include <compare>
#include <cstddef>
#include <map>
#include <span>
#include <vector>

#include "timetable/domain/assignment/search.hpp"
#include "timetable/domain/assignment/split.hpp"
#include "timetable/domain/model.hpp"

namespace timetable::domain::assignment::detail::grouping {

    struct OdKey final {
        ZoneId origin;
        ZoneId destination;

        auto operator<=>(const OdKey&) const = default;
    };

    struct DemandKey final {
        ZoneId     origin;
        ZoneId     destination;
        IntervalId interval;

        auto operator<=>(const DemandKey&) const = default;
    };

    struct ConnectionTraceKey final {
        ZoneId                           origin;
        ZoneId                           destination;
        std::vector<ConnectionSegmentId> segments{};

        auto operator<=>(const ConnectionTraceKey&) const = default;
    };

    using OwnedOdConnectionGroups    = std::map<OdKey    , std::vector<SearchConnection>>;
    using BorrowedOdConnectionGroups = std::map<OdKey    , std::vector<const SearchConnection*>>;
    using DemandEntryGroups          = std::map<OdKey    , std::vector<const DemandEntry*>>;
    using ShareGroups                = std::map<DemandKey, std::vector<const ConnectionDemandShare*>>;

    [[nodiscard]] inline OdKey od_key(
        const SearchConnection& connection
    ) noexcept {
        return OdKey{
              .origin      = origin_of(connection)
            , .destination = destination_of(connection)
        };
    }

    [[nodiscard]] inline OdKey od_key(
        const DemandEntry& demand
    ) noexcept {
        return OdKey{
              .origin      = demand.origin
            , .destination = demand.destination
        };
    }

    [[nodiscard]] inline ConnectionTraceKey connection_trace_key(
        const SearchConnection& connection
    ) {
        return ConnectionTraceKey{
              .origin      = origin_of(connection)
            , .destination = destination_of(connection)
            , .segments    = connection_segment_trace(connection)
        };
    }

    [[nodiscard]] inline DemandKey demand_key(
        const DemandEntry& demand
    ) noexcept {
        return DemandKey{
              .origin      = demand.origin
            , .destination = demand.destination
            , .interval    = demand.interval
        };
    }

    [[nodiscard]] inline DemandKey demand_key(
        const ConnectionDemandShare& share
    ) noexcept {
        return DemandKey{
              .origin      = share.origin
            , .destination = share.destination
            , .interval    = share.interval
        };
    }

    [[nodiscard]] inline DemandKey demand_key(
        const UnassignedDemand& demand
    ) noexcept {
        return DemandKey{
              .origin      = demand.origin
            , .destination = demand.destination
            , .interval    = demand.interval
        };
    }

    [[nodiscard]] inline std::map<OdKey, std::size_t> count_connections_by_od(
        std::span<const SearchConnection> connections
    ) {
        std::map<OdKey, std::size_t> counts;
        for (const auto& connection : connections) {
            ++counts[od_key(connection)];
        }
        return counts;
    }

    [[nodiscard]] inline std::map<ConnectionTraceKey, std::size_t> trace_index_map(
        std::span<const SearchConnection> connections
    ) {
        std::map<ConnectionTraceKey, std::size_t> indices;
        for (std::size_t i = 0; i < connections.size(); ++i) {
            indices.emplace(connection_trace_key(connections[i]), i);
        }
        return indices;
    }

    [[nodiscard]] inline std::map<ConnectionTraceKey, std::size_t> trace_index_map(
        const std::vector<const SearchConnection*>& connections
    ) {
        std::map<ConnectionTraceKey, std::size_t> indices;
        for (std::size_t i = 0; i < connections.size(); ++i) {
            indices.emplace(connection_trace_key(*connections[i]), i);
        }
        return indices;
    }

    [[nodiscard]] inline OwnedOdConnectionGroups group_connections_by_od(
        std::span<const SearchConnection> connections
    ) {
        OwnedOdConnectionGroups groups;
        for (const auto& connection : connections) {
            groups[od_key(connection)].push_back(connection);
        }
        return groups;
    }

    [[nodiscard]] inline BorrowedOdConnectionGroups group_connection_ptrs_by_od(
        std::span<const SearchConnection> connections
    ) {
        BorrowedOdConnectionGroups groups;
        for (const auto& connection : connections) {
            groups[od_key(connection)].push_back(&connection);
        }
        return groups;
    }

    [[nodiscard]] inline BorrowedOdConnectionGroups group_connection_ptrs_by_od(
        const OdDayPathChoiceResult& result
    ) {
        BorrowedOdConnectionGroups groups;
        for (const auto& origin_result : result.origin_results) {
            for (const auto& pair_result : origin_result.pair_results) {
                const auto key = OdKey{
                      .origin      = pair_result.origin
                    , .destination = pair_result.destination
                };
                for (const auto& connection : pair_result.connections) {
                    groups[key].push_back(&connection);
                }
            }
        }
        return groups;
    }

    [[nodiscard]] inline BorrowedOdConnectionGroups group_connection_ptrs_by_od(
        const ConnectionSearchResult& result
    ) {
        BorrowedOdConnectionGroups groups;
        for (const auto& task_result : result.task_results) {
            for (const auto& connection : task_result.connections) {
                groups[od_key(connection)].push_back(&connection);
            }
        }
        return groups;
    }

    [[nodiscard]] inline std::map<OdKey, std::size_t> count_connections_by_od(
        const ConnectionSearchResult& result
    ) {
        std::map<OdKey, std::size_t> counts;
        for (const auto& task_result : result.task_results) {
            for (const auto& connection : task_result.connections) {
                ++counts[od_key(connection)];
            }
        }
        return counts;
    }

    [[nodiscard]] inline DemandEntryGroups group_demand_entries_by_od(
        std::span<const DemandEntry> demand_entries
    ) {
        DemandEntryGroups groups;
        for (const auto& demand : demand_entries) {
            groups[od_key(demand)].push_back(&demand);
        }
        return groups;
    }

    [[nodiscard]] inline ShareGroups group_shares_by_demand_key(
        std::span<const ConnectionDemandShare> shares
    ) {
        ShareGroups groups;
        for (const auto& share : shares) {
            groups[demand_key(share)].push_back(&share);
        }
        return groups;
    }

}  // namespace timetable::domain::assignment::detail::grouping
