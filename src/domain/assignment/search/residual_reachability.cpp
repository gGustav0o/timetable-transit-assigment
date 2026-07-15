#include "timetable/domain/assignment/search/residual_reachability.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <queue>
#include <unordered_map>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/impedance.hpp"
#include "timetable/domain/segment_semantics.hpp"

namespace timetable::domain::assignment {

    bool operator==(
          const ResidualReachabilityKey& lhs
        , const ResidualReachabilityKey& rhs
    ) noexcept {
        return lhs.current_physical    == rhs.current_physical
            && lhs.phase               == rhs.phase
            && lhs.remaining_transfers == rhs.remaining_transfers;
    }

    std::size_t ResidualReachabilityKeyHash::operator()(
        const ResidualReachabilityKey& key
    ) const noexcept {
        std::size_t seed = 17u;
        seed = seed * 31u + std::hash<EndpointKey>{}(key.current_physical);
        seed = seed * 31u + std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(key.phase));
        seed = seed * 31u + std::hash<std::int32_t>{}(key.remaining_transfers.get());
        return seed;
    }

    namespace {

    struct ResidualPhysicalEdgeKey final {
        EndpointKey from{};
        EndpointKey to{};
    };

    [[nodiscard]] bool operator==(
          const ResidualPhysicalEdgeKey& lhs
        , const ResidualPhysicalEdgeKey& rhs
    ) noexcept {
        return lhs.from == rhs.from && lhs.to == rhs.to;
    }

    struct ResidualPhysicalEdgeKeyHash final {
        std::size_t operator()(const ResidualPhysicalEdgeKey& key) const noexcept {
            std::size_t seed = 17u;
            seed = seed * 31u + std::hash<EndpointKey>{}(key.from);
            seed = seed * 31u + std::hash<EndpointKey>{}(key.to);
            return seed;
        }
    };

    }  // namespace

    using ResidualPhysicalEdgeTimes = std::unordered_map<
          ResidualPhysicalEdgeKey
        , Time
        , ResidualPhysicalEdgeKeyHash
    >;

    void retain_min_residual_edge_time(
          ResidualPhysicalEdgeTimes& edge_times
        , ResidualPhysicalEdgeKey    key
        , Time                       run_time
    ) {
        const auto [it, inserted] = edge_times.emplace(key, run_time);
        if (!inserted && run_time.value() < it->second.value()) {
            it->second = run_time;
        }
    }

    void append_residual_reverse_edges(
          std::unordered_map<EndpointKey, std::vector<ResidualReverseEdge>>& target
        , const ResidualPhysicalEdgeTimes&                                   edge_times
    ) {
        for (const auto& [key, run_time] : edge_times) {
            target[key.to].push_back(
                ResidualReverseEdge{
                      .predecessor = key.from
                    , .run_time    = run_time
                }
            );
            target.try_emplace(key.from);
        }
    }

    [[nodiscard]] ResidualReachabilityKey residual_reachability_key(
        const RelaxedSuffixState& state
    ) noexcept {
        return ResidualReachabilityKey{
              .current_physical    = state.current_physical
            , .phase               = state.phase
            , .remaining_transfers = state.remaining_transfers
        };
    }

    [[nodiscard]] ResidualReverseGraph build_residual_reverse_graph(
          std::span<const RouteSegment>      route_segments
        , std::span<const ConnectionSegment> connection_segments
    ) {
        ResidualPhysicalEdgeTimes walk_edges;
        ResidualPhysicalEdgeTimes timed_edges;

        for (const auto& segment : route_segments) {
            if (is_walk(segment)) {
                retain_min_residual_edge_time(
                      walk_edges
                    , ResidualPhysicalEdgeKey{
                          .from = physical_from_key(segment)
                        , .to   = physical_to_key(segment)
                      }
                    , segment.run_time
                );
            }
        }

        for (const auto& segment : connection_segments) {
            if (!is_timed_connection(segment)
                || !segment.departure.has_value()
                || !segment.arrival.has_value()) {
                continue;
            }
            const auto& route_segment = route_segments[static_cast<std::size_t>(
                segment.route_segment.get()
            )];
            retain_min_residual_edge_time(
                  timed_edges
                , ResidualPhysicalEdgeKey{
                      .from = physical_from_key(route_segment)
                    , .to   = physical_to_key(route_segment)
                  }
                , Time{ segment.arrival->value() - segment.departure->value() }
            );
        }

        ResidualReverseGraph graph;
        append_residual_reverse_edges(graph.walk_predecessors_by_node, walk_edges);
        append_residual_reverse_edges(graph.timed_predecessors_by_node, timed_edges);
        return graph;
    }

    [[nodiscard]] bool has_reachable_state(
          const DestinationResidualReachability& destination
        , const ResidualReachabilityKey&         state
    ) noexcept {
        return destination.reachable_states.contains(state);
    }

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

    [[nodiscard]] bool has_more_budget_state(
          const DestinationResidualReachability& destination
        , const ResidualReachabilityKey&         state
        , TransferCount                          max_transfers
    ) noexcept {
        auto next_remaining = bounded_next_transfer_count(
              state.remaining_transfers
            , max_transfers
        );
        while (next_remaining.has_value()) {
            if (has_reachable_state(
                  destination
                , ResidualReachabilityKey{
                      .current_physical    = state.current_physical
                    , .phase               = state.phase
                    , .remaining_transfers = *next_remaining
                  }
            )) {
                return true;
            }
            next_remaining = bounded_next_transfer_count(*next_remaining, max_transfers);
        }
        return false;
    }

    [[nodiscard]] bool has_any_phase_state_at_endpoint(
          const DestinationResidualReachability& destination
        , EndpointKey                            endpoint
    ) noexcept {
        return std::any_of(
              destination.reachable_states.begin()
            , destination.reachable_states.end()
            , [&](const ResidualReachabilityKey& state) {
                return state.current_physical == endpoint;
            }
        );
    }

    void enqueue_reachable_state(
          DestinationResidualReachability& destination
        , std::deque<ResidualReachabilityKey>& frontier
        , ResidualReachabilityKey              state
    ) {
        if (destination.reachable_states.insert(state).second) {
            frontier.push_back(state);
        }
    }

    void enqueue_walk_predecessors(
          const ResidualReverseGraph&          graph
        , DestinationResidualReachability&      destination
        , std::deque<ResidualReachabilityKey>& frontier
        , const ResidualReachabilityKey&        state
    ) {
        const auto predecessors = graph.walk_predecessors_by_node.find(state.current_physical);
        if (predecessors == graph.walk_predecessors_by_node.end()) {
            return;
        }

        if (state.phase == SearchBranchPhase::Completed) {
            // Reverse of AfterTimedRide --egress walk--> Completed.
            for (const auto edge : predecessors->second) {
                const auto predecessor = edge.predecessor;
                if (predecessor.kind != EndpointKind::Stop) {
                    continue;
                }
                enqueue_reachable_state(
                      destination
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = predecessor
                        , .phase               = SearchBranchPhase::AfterTimedRide
                        , .remaining_transfers = state.remaining_transfers
                      }
                );
            }
            return;
        }

        if (state.phase == SearchBranchPhase::AfterTransferWalk) {
            // Reverse of AfterTimedRide --transfer walk--> AfterTransferWalk.
            for (const auto edge : predecessors->second) {
                const auto predecessor = edge.predecessor;
                if (predecessor.kind != EndpointKind::Stop) {
                    continue;
                }
                enqueue_reachable_state(
                      destination
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = predecessor
                        , .phase               = SearchBranchPhase::AfterTimedRide
                        , .remaining_transfers = state.remaining_transfers
                      }
                );
            }
            return;
        }

        if (state.phase == SearchBranchPhase::BeforeFirstBoarding) {
            // Reverse of AtOrigin --access walk--> BeforeFirstBoarding.
            for (const auto edge : predecessors->second) {
                const auto predecessor = edge.predecessor;
                if (predecessor.kind != EndpointKind::Zone) {
                    continue;
                }
                enqueue_reachable_state(
                      destination
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = predecessor
                        , .phase               = SearchBranchPhase::AtOrigin
                        , .remaining_transfers = state.remaining_transfers
                      }
                );
            }
        }
    }

    void enqueue_timed_predecessors(
          const ResidualReverseGraph&          graph
        , DestinationResidualReachability&      destination
        , std::deque<ResidualReachabilityKey>& frontier
        , const ResidualReachabilityKey&        state
        , TransferCount                         max_transfers
    ) {
        if (state.phase != SearchBranchPhase::AfterTimedRide) {
            return;
        }

        const auto predecessors = graph.timed_predecessors_by_node.find(state.current_physical);
        if (predecessors == graph.timed_predecessors_by_node.end()) {
            return;
        }

        for (const auto edge : predecessors->second) {
            const auto predecessor = edge.predecessor;
            if (predecessor.kind != EndpointKind::Stop) {
                continue;
            }

            // First timed boarding does not count as a transfer.
            enqueue_reachable_state(
                  destination
                , frontier
                , ResidualReachabilityKey{
                      .current_physical    = predecessor
                    , .phase               = SearchBranchPhase::BeforeFirstBoarding
                    , .remaining_transfers = state.remaining_transfers
                  }
            );

            // Every later timed boarding consumes one remaining transfer.
            if (const auto next_remaining = bounded_next_transfer_count(
                  state.remaining_transfers
                , max_transfers
            )) {
                // Reverse of AfterTimedRide --timed ride--> AfterTimedRide.
                enqueue_reachable_state(
                      destination
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = predecessor
                        , .phase               = SearchBranchPhase::AfterTimedRide
                        , .remaining_transfers = *next_remaining
                      }
                );
                // Reverse of AfterTransferWalk --timed ride--> AfterTimedRide.
                enqueue_reachable_state(
                      destination
                    , frontier
                    , ResidualReachabilityKey{
                          .current_physical    = predecessor
                        , .phase               = SearchBranchPhase::AfterTransferWalk
                        , .remaining_transfers = *next_remaining
                      }
                );
            }
        }
    }

    enum class ResidualTransitionKind : std::uint8_t {
          AccessWalk
        , TransferWalk
        , EgressWalk
        , FirstTimedRide
        , TransferTimedRide
    };

    struct ResidualPredecessorTransition final {
        ResidualReachabilityKey predecessor{};
        Time                    run_time{};
        ResidualTransitionKind  kind{ ResidualTransitionKind::AccessWalk };
    };

    template <typename Visitor>
    void for_each_residual_predecessor_transition(
          const ResidualReverseGraph&   graph
        , const ResidualReachabilityKey& state
        , TransferCount                  max_transfers
        , Visitor&&                      visit
    ) {
        auto&& visitor = visit;

        const auto walk_predecessors = graph.walk_predecessors_by_node.find(state.current_physical);
        if (walk_predecessors != graph.walk_predecessors_by_node.end()) {
            if (state.phase == SearchBranchPhase::Completed) {
                for (const auto edge : walk_predecessors->second) {
                    if (edge.predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTimedRide
                              , .remaining_transfers = state.remaining_transfers
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::EgressWalk
                    });
                }
            } else if (state.phase == SearchBranchPhase::AfterTransferWalk) {
                for (const auto edge : walk_predecessors->second) {
                    if (edge.predecessor.kind != EndpointKind::Stop) {
                        continue;
                    }
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AfterTimedRide
                              , .remaining_transfers = state.remaining_transfers
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::TransferWalk
                    });
                }
            } else if (state.phase == SearchBranchPhase::BeforeFirstBoarding) {
                for (const auto edge : walk_predecessors->second) {
                    if (edge.predecessor.kind != EndpointKind::Zone) {
                        continue;
                    }
                    visitor(ResidualPredecessorTransition{
                          .predecessor = ResidualReachabilityKey{
                                .current_physical    = edge.predecessor
                              , .phase               = SearchBranchPhase::AtOrigin
                              , .remaining_transfers = state.remaining_transfers
                            }
                        , .run_time    = edge.run_time
                        , .kind        = ResidualTransitionKind::AccessWalk
                    });
                }
            }
        }

        if (state.phase != SearchBranchPhase::AfterTimedRide) {
            return;
        }

        const auto timed_predecessors = graph.timed_predecessors_by_node.find(state.current_physical);
        if (timed_predecessors == graph.timed_predecessors_by_node.end()) {
            return;
        }

        for (const auto edge : timed_predecessors->second) {
            if (edge.predecessor.kind != EndpointKind::Stop) {
                continue;
            }

            visitor(ResidualPredecessorTransition{
                  .predecessor = ResidualReachabilityKey{
                        .current_physical    = edge.predecessor
                      , .phase               = SearchBranchPhase::BeforeFirstBoarding
                      , .remaining_transfers = state.remaining_transfers
                    }
                , .run_time    = edge.run_time
                , .kind        = ResidualTransitionKind::FirstTimedRide
            });

            if (const auto next_remaining = bounded_next_transfer_count(
                  state.remaining_transfers
                , max_transfers
            )) {
                visitor(ResidualPredecessorTransition{
                      .predecessor = ResidualReachabilityKey{
                            .current_physical    = edge.predecessor
                          , .phase               = SearchBranchPhase::AfterTimedRide
                          , .remaining_transfers = *next_remaining
                        }
                    , .run_time    = edge.run_time
                    , .kind        = ResidualTransitionKind::TransferTimedRide
                });
                visitor(ResidualPredecessorTransition{
                      .predecessor = ResidualReachabilityKey{
                            .current_physical    = edge.predecessor
                          , .phase               = SearchBranchPhase::AfterTransferWalk
                          , .remaining_transfers = *next_remaining
                        }
                    , .run_time    = edge.run_time
                    , .kind        = ResidualTransitionKind::TransferTimedRide
                });
            }
        }
    }

    [[nodiscard]] double residual_transition_journey_time(
        const ResidualPredecessorTransition& transition
    ) noexcept {
        return transition.run_time.value();
    }

    [[nodiscard]] double residual_transition_transfer_count(
        const ResidualPredecessorTransition& transition
    ) noexcept {
        return transition.kind == ResidualTransitionKind::TransferTimedRide ? 1.0 : 0.0;
    }

    [[nodiscard]] double residual_transition_impedance(
          const ResidualPredecessorTransition& transition
        , const SearchImpedance&               impedance
        , double                               fare_scale
    ) noexcept {
        ConnectionImpedanceComponents components{};
        switch (transition.kind) {
            case ResidualTransitionKind::AccessWalk:
                components.access_time = transition.run_time;
                break;
            case ResidualTransitionKind::TransferWalk:
                components.transfer_walk_time = transition.run_time;
                break;
            case ResidualTransitionKind::EgressWalk:
                components.egress_time = transition.run_time;
                break;
            case ResidualTransitionKind::FirstTimedRide:
                components.in_vehicle_time = transition.run_time;
                break;
            case ResidualTransitionKind::TransferTimedRide:
                components.in_vehicle_time = transition.run_time;
                components.transfer_count  = TransferCount{ 1 };
                break;
        }
        return connection_impedance_value(components, impedance, fare_scale);
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

            for_each_residual_predecessor_transition(
                  graph
                , item.state
                , max_transfers
                , [&](const ResidualPredecessorTransition& transition) {
                    const auto candidate =
                        item.distance + edge_cost(transition);
                    const auto known = distances.find(transition.predecessor);
                    if (known != distances.end() && known->second <= candidate) {
                        return;
                    }
                    distances[transition.predecessor] = candidate;
                    frontier.push(ResidualDistanceQueueItem{
                          .distance = candidate
                        , .state    = transition.predecessor
                    });
                }
            );
        }

        return distances;
    }

    [[nodiscard]] std::unordered_map<
          ResidualReachabilityKey
        , ResidualSuffixLowerBounds
        , ResidualReachabilityKeyHash
    > build_residual_suffix_lower_bounds(
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

        std::unordered_map<
              ResidualReachabilityKey
            , ResidualSuffixLowerBounds
            , ResidualReachabilityKeyHash
        > lower_bounds;
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

    [[nodiscard]] DestinationResidualReachability build_destination_residual_reachability(
          const ResidualReverseGraph& graph
        , ZoneId                      destination
        , TransferCount               max_transfers
        , const SearchImpedance&      impedance
        , double                      fare_scale
    ) {
        DestinationResidualReachability reachability;
        std::deque<ResidualReachabilityKey> frontier;
        const auto destination_endpoint = endpoint_key(destination);

        for_each_transfer_count_up_to(max_transfers, [&](const TransferCount remaining) {
            enqueue_reachable_state(
                  reachability
                , frontier
                , ResidualReachabilityKey{
                      .current_physical    = destination_endpoint
                    , .phase               = SearchBranchPhase::Completed
                    , .remaining_transfers = remaining
                  }
            );
        });

        while (!frontier.empty()) {
            const auto state = frontier.front();
            frontier.pop_front();

            enqueue_walk_predecessors(
                  graph
                , reachability
                , frontier
                , state
            );
            enqueue_timed_predecessors(
                  graph
                , reachability
                , frontier
                , state
                , max_transfers
            );
        }

        reachability.suffix_lower_bounds = build_residual_suffix_lower_bounds(
              graph
            , destination
            , max_transfers
            , impedance
            , fare_scale
        );

        return reachability;
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

    namespace {

    mathfp::Expected<mathfp::Unit> validate_destination_residual_reachability(
          ZoneId                                destination
        , const DestinationResidualReachability& reachability
        , TransferCount                         max_transfers
    ) {
        const auto destination_endpoint = endpoint_key(destination);

        auto remaining = TransferCount{0};
        while (remaining <= max_transfers) {
            if (!has_reachable_state(
                  reachability
                , ResidualReachabilityKey{
                      .current_physical    = destination_endpoint
                    , .phase               = SearchBranchPhase::Completed
                    , .remaining_transfers = remaining
                  }
            )) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability misses destination completion seed")
                        .ctx("destination", destination.get())
                        .ctx("remaining_transfers", remaining.get())
                );
            }

            const auto next = bounded_next_transfer_count(remaining, max_transfers);
            if (!next.has_value()) {
                break;
            }
            remaining = *next;
        }

        for (const auto& state : reachability.reachable_states) {
            if (state.remaining_transfers.get() < 0
                || state.remaining_transfers.get() > max_transfers.get()) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains state outside transfer budget")
                        .ctx("destination", destination.get())
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                        .ctx("max_transfers", max_transfers.get())
                );
            }

            if (state.phase == SearchBranchPhase::Completed
                && state.current_physical != destination_endpoint) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains non-destination completed state")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                );
            }

            if (state.phase == SearchBranchPhase::AtOrigin
                && state.current_physical.kind != EndpointKind::Zone) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains at-origin state outside zone")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }

            if ((state.phase == SearchBranchPhase::BeforeFirstBoarding
                    || state.phase == SearchBranchPhase::AfterTimedRide
                    || state.phase == SearchBranchPhase::AfterTransferWalk)
                && state.current_physical.kind != EndpointKind::Stop) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains stop-phase state outside stop")
                        .ctx("destination", destination.get())
                        .ctx("phase", static_cast<std::int64_t>(state.phase))
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }

            if (state.phase == SearchBranchPhase::Completed
                && state.current_physical.kind != EndpointKind::Zone) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability contains completed state outside zone")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }

            if (const auto next_remaining = bounded_next_transfer_count(
                  state.remaining_transfers
                , max_transfers
            )) {
                const auto relaxed_more_budget_state = ResidualReachabilityKey{
                      .current_physical    = state.current_physical
                    , .phase               = state.phase
                    , .remaining_transfers = *next_remaining
                };
                if (!has_reachable_state(reachability, relaxed_more_budget_state)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("residual reachability violates transfer-budget monotonicity")
                            .ctx("destination", destination.get())
                            .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                            .ctx("endpoint_id", state.current_physical.id)
                            .ctx("remaining_transfers", state.remaining_transfers.get())
                    );
                }
            }

            const auto lower_bounds = reachability.suffix_lower_bounds.find(state);
            if (lower_bounds == reachability.suffix_lower_bounds.end()) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual reachability state misses suffix lower bounds")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }
            if (
                   !std::isfinite(lower_bounds->second.journey_time.value())
                || !std::isfinite(lower_bounds->second.impedance)
                || lower_bounds->second.journey_time.value() < 0.0
                || lower_bounds->second.impedance < 0.0
                || lower_bounds->second.transfers.get() < 0
            ) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual suffix lower bounds contain invalid value")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                );
            }
        }

        for (const auto& [state, lower_bounds] : reachability.suffix_lower_bounds) {
            (void)lower_bounds;
            if (!has_reachable_state(reachability, state)) {
                return mathfp::unexpected(
                    mathfp::internal_error("residual suffix lower bounds contain unreachable state")
                        .ctx("destination", destination.get())
                        .ctx("endpoint_kind", static_cast<std::int64_t>(state.current_physical.kind))
                        .ctx("endpoint_id", state.current_physical.id)
                        .ctx("remaining_transfers", state.remaining_transfers.get())
                );
            }
        }

        return mathfp::kUnit;
    }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_residual_reachability(
          const ResidualReachability& reachability
        , TransferCount               max_transfers
    ) {
        for (const auto& [destination, destination_reachability] : reachability.destinations) {
            MATHFP_TRY(validate_destination_residual_reachability(
                  destination
                , destination_reachability
                , max_transfers
            ));
        }
        return mathfp::kUnit;
    }

    [[nodiscard]] ReachabilityDecision evaluate_residual_reachability(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept {
        const auto destination_it = reachability.destinations.find(state.destination);
        if (destination_it == reachability.destinations.end()) {
            return ReachabilityDecision{ .feasible = true };
        }

        if (is_completed(state.phase)) {
            const auto feasible = state.current_physical.kind == EndpointKind::Zone
                && state.current_physical.id == state.destination.get();
            return ReachabilityDecision{
                  .feasible = feasible
                , .rejection_reason = feasible
                    ? ReachabilityRejectionReason::UnreachableDestination
                    : ReachabilityRejectionReason::Phase
            };
        }

        const auto key = residual_reachability_key(state);
        const auto& destination_reachability = destination_it->second;
        if (has_reachable_state(destination_reachability, key)) {
            return ReachabilityDecision{ .feasible = true };
        }

        if (has_more_budget_state(destination_reachability, key, max_transfers)) {
            return ReachabilityDecision{
                  .feasible = false
                , .rejection_reason = ReachabilityRejectionReason::TransferBudget
            };
        }

        if (has_any_phase_state_at_endpoint(destination_reachability, state.current_physical)) {
            return ReachabilityDecision{
                  .feasible = false
                , .rejection_reason = ReachabilityRejectionReason::Phase
            };
        }

        return ReachabilityDecision{
              .feasible = false
            , .rejection_reason = ReachabilityRejectionReason::UnreachableDestination
        };
    }

    [[nodiscard]] bool can_have_feasible_suffix(
          const ResidualReachability& reachability
        , const RelaxedSuffixState&   state
        , TransferCount               max_transfers
    ) noexcept {
        return evaluate_residual_reachability(
              reachability
            , state
            , max_transfers
        ).feasible;
    }

}  // namespace timetable::domain::assignment
