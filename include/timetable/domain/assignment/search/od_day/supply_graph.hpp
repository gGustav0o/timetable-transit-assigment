#pragma once

#include <cstddef>
#include <unordered_map>
#include <vector>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/od_day_path_contract.hpp"
#include "timetable/domain/assignment/search/model/support.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment {

    struct DayLevelWalkSupport final {
        DayPathLeg                       structural_leg{};
        DayLevelSupplyEdgeRef            edge;
        std::vector<ConnectionSegmentId> support_labels{};
    };

    struct DayLevelRideSupport final {
        DayPathLeg                       structural_leg{};
        DayLevelSupplyEdgeRef            edge;
        std::vector<ConnectionSegmentId> support_labels{};
    };

    using DayLevelTimedSupportLabel = TimedSupportLabel;

    struct DayLevelSupplySearchGraph final {
        DayLevelSupplyGraph graph{};
        std::unordered_map<EndpointKey, std::vector<DayLevelWalkSupport>> access_walks_by_from{};
        std::unordered_map<EndpointKey, std::vector<DayLevelWalkSupport>> transfer_walks_by_from{};
        std::unordered_map<EndpointKey, std::vector<DayLevelWalkSupport>> egress_walks_by_from{};
        std::unordered_map<EndpointKey, std::vector<DayLevelRideSupport>> rides_by_from{};
    };

    struct DayLevelSupplySearchProfile final {
        std::size_t access_walk_edges{};
        std::size_t transfer_walk_edges{};
        std::size_t egress_walk_edges{};
        std::size_t access_walk_labels{};
        std::size_t transfer_walk_labels{};
        std::size_t egress_walk_labels{};
        std::size_t ride_edges{};
        std::size_t ride_support_labels{};
    };

    [[nodiscard]] DayLevelSupplySearchProfile summarize_day_level_supply_search_profile(
        const DayLevelSupplySearchGraph& graph
    ) noexcept;

    mathfp::Expected<mathfp::Unit> validate_production_day_level_supply_graph(
        const DayLevelSupplySearchGraph& day_graph
    );

    [[nodiscard]] DayLevelSupplySearchGraph build_day_level_supply_search_graph(
        const PreprocessedNetwork& network
    );

}  // namespace timetable::domain::assignment
