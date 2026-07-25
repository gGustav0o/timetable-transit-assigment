#include "timetable/domain/assignment/day_path/signature.hpp"

#include <optional>
#include <utility>

namespace timetable::domain::assignment {

    DayPathLeg day_path_leg_of(
        const ConnectionLeg& leg
    ) noexcept {
        return production_day_path_leg(DayPathLeg{
              .kind            = leg.kind
            , .route_segment   = leg.route_segment
            , .physical_from   = leg.physical_from
            , .physical_to     = leg.physical_to
            , .occurrence_from = leg.occurrence_from
            , .occurrence_to   = leg.occurrence_to
            , .line            = leg.line
            , .route           = leg.route
        });
    }

    DayPathLeg production_day_path_leg(
        DayPathLeg leg
    ) noexcept {
        leg.route_segment   = std::nullopt;
        leg.occurrence_from = std::nullopt;
        leg.occurrence_to   = std::nullopt;
        if (!is_ride_leg(leg.kind)) {
            leg.line  = std::nullopt;
            leg.route = std::nullopt;
        }
        return leg;
    }

    DayPathPrefix make_day_path_prefix(
        ZoneId origin
    ) {
        return DayPathPrefix{
              .origin = origin
            , .legs   = {}
        };
    }

    DayPathPrefix append_day_path_leg(
          DayPathPrefix prefix
        , DayPathLeg    leg
    ) {
        prefix.legs.push_back(production_day_path_leg(std::move(leg)));
        return prefix;
    }

    DayPathSignature make_day_path_signature_from_tree_label(
          DayPathPrefix prefix
        , ZoneId        destination
    ) {
        for (auto& leg : prefix.legs) {
            leg = production_day_path_leg(std::move(leg));
        }
        return DayPathSignature{
              .origin      = prefix.origin
            , .destination = destination
            , .legs        = std::move(prefix.legs)
        };
    }

    DayPathSignature complete_day_path_signature(
          DayPathPrefix prefix
        , ZoneId        destination
    ) {
        return make_day_path_signature_from_tree_label(
              std::move(prefix)
            , destination
        );
    }

    DayPathSignature day_path_signature_of(
        const SearchConnection& connection
    ) {
        const auto& canonical = canonical_connection(connection);
        DayPathSignature signature{
              .origin      = canonical.origin
            , .destination = canonical.destination
            , .legs        = {}
        };
        signature.legs.reserve(canonical.trace.legs.size());
        for (const auto& leg : canonical.trace.legs) {
            if (is_wait_leg(leg.kind)) {
                continue;
            }
            signature.legs.push_back(day_path_leg_of(leg));
        }
        return signature;
    }

    const DayPathSignature& day_path_signature_of(
        const DayPathAlternative& alternative
    ) noexcept {
        return alternative.identity.signature;
    }

}  // namespace timetable::domain::assignment
