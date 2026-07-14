#include "timetable/domain/assignment/search/model/support.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "timetable/domain/assignment/day_path.hpp"

namespace timetable::domain::assignment {

    OdDayPathPrefix make_od_day_path_prefix(
        ZoneId origin
    ) noexcept {
        return OdDayPathPrefix{
              .origin = origin
            , .tail   = nullptr
            , .length = 0u
        };
    }

    OdDayPathPrefix append_od_day_path_leg(
          OdDayPathPrefix prefix
        , DayPathLeg       leg
    ) {
        prefix.tail = std::make_shared<OdDayPathPrefixNode>(
            OdDayPathPrefixNode{
                  .parent = std::move(prefix.tail)
                , .leg    = production_day_path_leg(std::move(leg))
                , .length = prefix.length + 1u
            }
        );
        ++prefix.length;
        return prefix;
    }

    DayPathPrefix materialize_day_path_prefix(
        const OdDayPathPrefix& prefix
    ) {
        std::vector<DayPathLeg> reversed;
        reversed.reserve(prefix.length);
        for (auto node = prefix.tail; node != nullptr; node = node->parent) {
            reversed.push_back(node->leg);
        }
        std::reverse(reversed.begin(), reversed.end());
        return DayPathPrefix{
              .origin = prefix.origin
            , .legs   = std::move(reversed)
        };
    }

    OdDaySupportPrefix append_od_day_support_segment(
          OdDaySupportPrefix prefix
        , ConnectionSegmentId segment
    ) {
        const auto next_length = prefix != nullptr ? prefix->length + 1u : 1u;
        return std::make_shared<OdDaySupportPrefixNode>(
            OdDaySupportPrefixNode{
                  .parent  = std::move(prefix)
                , .segment = segment
                , .length  = next_length
            }
        );
    }

    std::vector<ConnectionSegmentId> materialize_od_day_support_segments(
        const OdDaySupportPrefix& prefix
    ) {
        std::vector<ConnectionSegmentId> reversed;
        reversed.reserve(prefix != nullptr ? prefix->length : 0u);
        for (auto node = prefix; node != nullptr; node = node->parent) {
            reversed.push_back(node->segment);
        }
        std::reverse(reversed.begin(), reversed.end());
        return reversed;
    }

}  // namespace timetable::domain::assignment
