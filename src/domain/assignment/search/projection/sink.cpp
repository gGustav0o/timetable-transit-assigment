#include "timetable/domain/assignment/search/projection/sink.hpp"

#include <utility>

#include "timetable/domain/assignment/search/projection/contract.hpp"

namespace timetable::domain::assignment {

    SearchProjectionSinkSet make_search_projection_sink_set(
        std::span<const SearchProjectionSlot> slots
    ) {
        std::map<ZoneId, std::size_t> positions;
        for (std::size_t slot_pos = 0; slot_pos < slots.size(); ++slot_pos) {
            positions[slots[slot_pos].destination] = slot_pos;
        }
        return SearchProjectionSinkSet{
              .slots = slots
            , .completion_target_slots =
                  completion_target_projection_slots(slots)
            , .od_day_slots = od_day_projection_slots(slots)
            , .position_by_destination = std::move(positions)
        };
    }

    std::vector<SearchProjectionRetention> make_search_projection_retentions(
        const SearchProjectionSinkSet& sinks
    ) {
        std::vector<SearchProjectionRetention> retentions;
        retentions.reserve(sinks.slots.size());
        for (const auto& slot : sinks.slots) {
            retentions.push_back(SearchProjectionRetention{ .slot = slot });
        }
        return retentions;
    }

}  // namespace timetable::domain::assignment
