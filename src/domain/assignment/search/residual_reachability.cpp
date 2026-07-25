#include "timetable/domain/assignment/search/residual_reachability.hpp"

#include "timetable/domain/assignment/search/residual/closure.hpp"
#include "timetable/domain/assignment/search/residual/lower_bound.hpp"

namespace timetable::domain::assignment {

    namespace {

    [[nodiscard]] DestinationResidualReachability build_destination_residual_reachability(
          const ResidualReverseGraph& graph
        , ZoneId                      destination
        , TransferCount               max_transfers
        , const SearchImpedance&      impedance
        , double                      fare_scale
    ) {
        return DestinationResidualReachability{
              .reachable_states = build_residual_reachable_states(
                    graph
                  , destination
                  , max_transfers
                )
            , .suffix_lower_bounds = build_residual_suffix_lower_bounds(
                    graph
                  , destination
                  , max_transfers
                  , impedance
                  , fare_scale
                )
        };
    }

    }  // namespace

    [[nodiscard]] ResidualReachability build_residual_reachability(
          const ResidualReverseGraph& graph
        , std::span<const SearchCompletionTarget> targets
        , TransferCount               max_transfers
        , const SearchImpedance&      impedance
        , double                      fare_scale
    ) {
        ResidualReachability reachability;

        for (const auto& target : targets) {
            const auto destination = target.destination;
            if (reachability.destinations.contains(destination)) {
                continue;
            }
            reachability.destinations.emplace(
                  destination
                , build_destination_residual_reachability(
                      graph
                    , destination
                    , max_transfers
                    , impedance
                    , fare_scale
                )
            );
        }

        return reachability;
    }
}  // namespace timetable::domain::assignment
