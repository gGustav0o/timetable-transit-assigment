#pragma once

#include <cstddef>
#include <map>
#include <span>
#include <vector>

#include "timetable/domain/assignment/search/projection.hpp"
#include "timetable/domain/endpoints.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Value-level view of the batch projection boundary.
     *
     * A sink set does not own retention state and performs no projection
     * mutation. It is the mathematical target family against which the tree
     * reports complete branches.
     */
    struct SearchProjectionSinkSet final {
        std::span<const SearchProjectionSlot> slots{};
        bool completion_target_slots{};
        bool od_day_slots{};
        std::map<ZoneId, std::size_t> position_by_destination{};

        [[nodiscard]] std::size_t size() const noexcept {
            return slots.size();
        }
    };

    [[nodiscard]] SearchProjectionSinkSet make_search_projection_sink_set(
        std::span<const SearchProjectionSlot> slots
    );

    [[nodiscard]] std::vector<SearchProjectionRetention>
    make_search_projection_retentions(
        const SearchProjectionSinkSet& sinks
    );

}  // namespace timetable::domain::assignment
