#pragma once

#include "timetable/domain/assignment/day_path/types.hpp"

namespace timetable::domain::assignment {

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

}  // namespace timetable::domain::assignment
