#include "timetable/domain/assignment/search/residual/lower_bound.hpp"

#include <cstdint>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include "timetable/domain/assignment/search/residual/transition.hpp"

namespace timetable::domain::assignment {
namespace {

    template <typename Fn>
    void for_each_transfer_count_up_to(TransferCount max_transfers, Fn&& fn) {
        auto remaining = TransferCount{0};
        while (remaining <= max_transfers) {
            fn(remaining);

            const auto next = bounded_next_transfer_count(remaining, max_transfers);
            if (!next.has_value()) {
                break;
            }
            remaining = *next;
        }
    }

    using ResidualDistanceMap = std::unordered_map<
          ResidualReachabilityKey
        , double
        , ResidualReachabilityKeyHash
    >;

    struct ResidualDistanceQueueItem final {
        double                  distance{};
        ResidualReachabilityKey state{};
    };

    struct ResidualDistanceQueueGreater final {
        bool operator()(
              const ResidualDistanceQueueItem& lhs
            , const ResidualDistanceQueueItem& rhs
        ) const noexcept {
            return lhs.distance > rhs.distance;
        }
    };

    template <typename EdgeCost>
    [[nodiscard]] ResidualDistanceMap compute_residual_suffix_distances(
          const ResidualReverseGraph& graph
        , ZoneId                      destination
        , TransferCount               max_transfers
        , EdgeCost&&                  edge_cost
    ) {
        ResidualDistanceMap distances;
        std::priority_queue<
              ResidualDistanceQueueItem
            , std::vector<ResidualDistanceQueueItem>
            , ResidualDistanceQueueGreater
        > frontier;

        const auto destination_endpoint = endpoint_key(destination);
        for_each_transfer_count_up_to(max_transfers, [&](const TransferCount remaining) {
            const auto seed = ResidualReachabilityKey{
                  .current_physical    = destination_endpoint
                , .phase               = SearchBranchPhase::Completed
                , .remaining_transfers = remaining
            };
            distances.emplace(seed, 0.0);
            frontier.push(ResidualDistanceQueueItem{
                  .distance = 0.0
                , .state    = seed
            });
        });

        while (!frontier.empty()) {
            const auto item = frontier.top();
            frontier.pop();

            const auto current_it = distances.find(item.state);
            if (current_it == distances.end() || item.distance != current_it->second) {
                continue;
            }

            for (const auto& transition : residual_predecessor_transitions(
                  graph
                , item.state
                , max_transfers
            )) {
                const auto candidate =
                    item.distance + edge_cost(transition);
                const auto known = distances.find(transition.predecessor);
                if (known != distances.end() && known->second <= candidate) {
                    continue;
                }
                distances[transition.predecessor] = candidate;
                frontier.push(ResidualDistanceQueueItem{
                      .distance = candidate
                    , .state    = transition.predecessor
                });
            }
        }

        return distances;
    }

}  // namespace

    [[nodiscard]] ResidualSuffixLowerBoundMap build_residual_suffix_lower_bounds(
          const ResidualReverseGraph& graph
        , ZoneId                      destination
        , TransferCount               max_transfers
        , const SearchImpedance&      impedance
        , double                      fare_scale
    ) {
        const auto journey_time_distances = compute_residual_suffix_distances(
              graph
            , destination
            , max_transfers
            , [](const ResidualPredecessorTransition& transition) {
                return residual_transition_journey_time(transition);
            }
        );
        const auto transfer_distances = compute_residual_suffix_distances(
              graph
            , destination
            , max_transfers
            , [](const ResidualPredecessorTransition& transition) {
                return residual_transition_transfer_count(transition);
            }
        );
        const auto impedance_distances = compute_residual_suffix_distances(
              graph
            , destination
            , max_transfers
            , [&](const ResidualPredecessorTransition& transition) {
                return residual_transition_impedance(
                      transition
                    , impedance
                    , fare_scale
                );
            }
        );

        ResidualSuffixLowerBoundMap lower_bounds;
        lower_bounds.reserve(journey_time_distances.size());
        for (const auto& [state, journey_time] : journey_time_distances) {
            const auto transfers = transfer_distances.find(state);
            const auto imp       = impedance_distances.find(state);
            if (transfers == transfer_distances.end() || imp == impedance_distances.end()) {
                continue;
            }
            lower_bounds.emplace(
                  state
                , ResidualSuffixLowerBounds{
                      .journey_time = Time{ journey_time }
                    , .transfers    = TransferCount{
                          static_cast<std::int32_t>(transfers->second)
                      }
                    , .impedance    = imp->second
                  }
            );
        }
        return lower_bounds;
    }

}  // namespace timetable::domain::assignment
