#pragma once

#include <cstddef>
#include <vector>

#include "timetable/domain/model.hpp"
#include "timetable/domain/assignment/search/connection.hpp"

namespace timetable::domain::assignment {

    struct AllZoneTargetResult final {
        ZoneId                        origin;
        ZoneId                        destination;
        std::size_t                   connection_count{};
        std::vector<SearchConnection> connections{};
    };

    struct AllZoneTreeResult final {
        ZoneId                           origin;
        std::vector<AllZoneTargetResult> target_results{};
    };

    struct AllZoneConnectionSearchResult final {
        std::vector<AllZoneTreeResult> tree_results{};
    };

    [[nodiscard]] std::size_t search_connection_count(
        const AllZoneConnectionSearchResult& result
    ) noexcept;

    [[nodiscard]] std::size_t all_zone_target_connection_count(
        const AllZoneTargetResult& target
    ) noexcept;

}  // namespace timetable::domain::assignment
