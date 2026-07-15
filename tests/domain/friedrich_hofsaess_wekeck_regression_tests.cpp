#include <algorithm>
#include <compare>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <numeric>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/choice/choice.hpp"
#include "timetable/domain/assignment/complete_connection_retention.hpp"
#include "timetable/domain/assignment/connection.hpp"
#include "timetable/domain/assignment/search/branch_and_bound_search.hpp"
#include "timetable/domain/assignment/search/generation/successor.hpp"
#include "timetable/domain/assignment/search/preprocessed_network.hpp"
#include "timetable/domain/assignment/search/request.hpp"
#include "timetable/domain/assignment/search/residual_reachability.hpp"
#include "timetable/domain/assignment/search_pruning_plan.hpp"
#include "timetable/domain/assignment/search_time_domain_execution.hpp"
#include "timetable/domain/assignment/split/split.hpp"
#include "timetable/domain/params/make.hpp"
#include "timetable/domain/preprocessing/segments_factory.hpp"

namespace timetable::domain::assignment {
namespace {

    constexpr auto kOrigin = std::int64_t{ 1 };
    constexpr auto kDestination = std::int64_t{ 2 };
    constexpr auto kStopA = std::int64_t{ 10 };
    constexpr auto kStation = std::int64_t{ 20 };
    constexpr auto kStopB = std::int64_t{ 30 };
    constexpr auto kStopX = std::int64_t{ 40 };
    constexpr auto kBusLine = std::int64_t{ 100 };
    constexpr auto kTrainLine = std::int64_t{ 200 };
    constexpr auto kBusRoute = std::int64_t{ 1000 };
    constexpr auto kTrainRoute = std::int64_t{ 2000 };
    constexpr auto kInterval = std::int64_t{ 1 };
    constexpr auto kDemand = 120.0;

    enum RouteIds : std::int64_t {
          kAccessRoute = 0
        , kEgressRoute = 1
        , kBusAToStation = 2
        , kBusAToB = 3
        , kBusAToX = 4
        , kBusStationToB = 5
        , kBusStationToX = 6
        , kBusBToX = 7
        , kTrainStationToX = 8
    };

    struct ExpectedConnection final {
        double       departure{};
        double       arrival{};
        std::int32_t transfers{};

        auto operator<=>(const ExpectedConnection&) const = default;
    };

    struct ExpectedConnectionMetrics final {
        ExpectedConnection connection{};
        double             journey_time{};
        double             in_vehicle_time{};
        double             transfer_wait_time{};
        double             split_impedance{};

        auto operator<=>(const ExpectedConnectionMetrics&) const = default;
    };

    struct SplitOracleAlternative final {
        ExpectedConnection key{};
        ConnectionMetrics  metrics{};
        double             perceived_journey_time{};
        double             independence{};
        double             probability{};
    };

    struct SplitShareSummary final {
        ExpectedConnection key{};
        double             independence{};
        double             split_impedance{};
        double             probability{};
        double             passengers{};

        auto operator<=>(const SplitShareSummary&) const = default;
    };

    struct ArticleScenario final {
        InputModel                  input{};
        PreprocessedNetwork         network{};
        SearchParams                params{};
        SearchCostContext           search_cost{};
        SearchPruningExecutionPlan  pruning{};
        std::vector<SearchTask>     tasks{};
        SearchTimeDomainExecution   time_domain_execution{};
        AssignmentPeriodConfig      assignment_period{};
        ConnectionAdmissibilityConfig admissibility{};
    };

    [[nodiscard]] StopOccurrence occurrence(
          std::int64_t stop
        , std::int64_t position
    ) noexcept {
        return StopOccurrence{
              .stop = StopId{ stop }
            , .position = RoutePosition{ position }
        };
    }

    [[nodiscard]] mathfp::Expected<mathfp::Unit> append(
          std::vector<RouteSegment>& target
        , mathfp::Expected<RouteSegment> segment
    ) {
        if (!segment) {
            return mathfp::unexpected(std::move(segment.error()));
        }
        target.push_back(std::move(*segment));
        return mathfp::kUnit;
    }

    [[nodiscard]] mathfp::Expected<mathfp::Unit> append(
          std::vector<ConnectionSegment>& target
        , mathfp::Expected<ConnectionSegment> segment
    ) {
        if (!segment) {
            return mathfp::unexpected(std::move(segment.error()));
        }
        target.push_back(std::move(*segment));
        return mathfp::kUnit;
    }

    [[nodiscard]] mathfp::Expected<std::vector<RouteSegment>> article_route_segments() {
        std::vector<RouteSegment> routes;
        routes.reserve(9u);

        MATHFP_TRY(append(
              routes
            , preprocessing::make_route_segment(
                  RouteSegmentId{ kAccessRoute }
                , WalkEndpoint{ ZoneId{ kOrigin } }
                , WalkEndpoint{ StopId{ kStopA } }
                , Length{ 0.0 }
                , Time{ 0.0 }
                , WalkPath{ WalkLinkId{ 0 } }
              )
        ));
        MATHFP_TRY(append(
              routes
            , preprocessing::make_route_segment(
                  RouteSegmentId{ kEgressRoute }
                , WalkEndpoint{ StopId{ kStopX } }
                , WalkEndpoint{ ZoneId{ kDestination } }
                , Length{ 0.0 }
                , Time{ 0.0 }
                , WalkPath{ WalkLinkId{ 1 } }
              )
        ));

        auto line = [&](RouteIds id, StopOccurrence from, StopOccurrence to, double run_time) {
            return preprocessing::make_route_segment(
                  RouteSegmentId{ id }
                , from
                , to
                , Length{ run_time }
                , Time{ run_time }
                , LineId{ id == kTrainStationToX ? kTrainLine : kBusLine }
                , RouteId{ id == kTrainStationToX ? kTrainRoute : kBusRoute }
            );
        };

        MATHFP_TRY(append(routes, line(kBusAToStation, occurrence(kStopA, 0), occurrence(kStation, 1), 12.0)));
        MATHFP_TRY(append(routes, line(kBusAToB, occurrence(kStopA, 0), occurrence(kStopB, 2), 32.0)));
        MATHFP_TRY(append(routes, line(kBusAToX, occurrence(kStopA, 0), occurrence(kStopX, 3), 45.0)));
        MATHFP_TRY(append(routes, line(kBusStationToB, occurrence(kStation, 1), occurrence(kStopB, 2), 20.0)));
        MATHFP_TRY(append(routes, line(kBusStationToX, occurrence(kStation, 1), occurrence(kStopX, 3), 33.0)));
        MATHFP_TRY(append(routes, line(kBusBToX, occurrence(kStopB, 2), occurrence(kStopX, 3), 13.0)));
        MATHFP_TRY(append(routes, line(kTrainStationToX, occurrence(kStation, 0), occurrence(kStopX, 1), 16.0)));

        return routes;
    }

    [[nodiscard]] mathfp::Expected<std::vector<ConnectionSegment>> article_connection_segments(
        const std::vector<RouteSegment>& routes
    ) {
        std::vector<ConnectionSegment> segments;
        segments.reserve(23u);
        auto next_id = std::int64_t{ 0 };

        auto route = [&](RouteIds id) -> const RouteSegment& {
            return routes.at(static_cast<std::size_t>(id));
        };
        auto walk = [&](RouteIds route_id) {
            return preprocessing::make_connection_segment(
                  ConnectionSegmentId{ next_id++ }
                , route(route_id)
                , std::nullopt
                , std::nullopt
                , std::nullopt
                , std::nullopt
                , std::nullopt
                , std::nullopt
            );
        };
        auto timed = [&](RouteIds route_id, std::int64_t trip, double dep, double arr) {
            const auto* line = line_topology_of(route(route_id));
            return preprocessing::make_connection_segment(
                  ConnectionSegmentId{ next_id++ }
                , route(route_id)
                , TripId{ trip }
                , line->from.position
                , line->to.position
                , Time{ dep }
                , Time{ arr }
                , std::nullopt
            );
        };

        MATHFP_TRY(append(segments, walk(kAccessRoute)));
        MATHFP_TRY(append(segments, walk(kEgressRoute)));

        struct BusTrip final {
            std::int64_t trip{};
            double a{};
            double station{};
            double b{};
            double x{};
        };
        const std::vector<BusTrip> bus_trips{
              BusTrip{ .trip = 1, .a = 10.0, .station = 22.0, .b = 42.0, .x = 55.0 }
            , BusTrip{ .trip = 2, .a = 55.0, .station = 67.0, .b = 87.0, .x = 100.0 }
            , BusTrip{ .trip = 3, .a = 85.0, .station = 97.0, .b = 117.0, .x = 130.0 }
        };
        for (const auto& trip : bus_trips) {
            MATHFP_TRY(append(segments, timed(kBusAToStation, trip.trip, trip.a, trip.station)));
            MATHFP_TRY(append(segments, timed(kBusAToB, trip.trip, trip.a, trip.b)));
            MATHFP_TRY(append(segments, timed(kBusAToX, trip.trip, trip.a, trip.x)));
            MATHFP_TRY(append(segments, timed(kBusStationToB, trip.trip, trip.station, trip.b)));
            MATHFP_TRY(append(segments, timed(kBusStationToX, trip.trip, trip.station, trip.x)));
            MATHFP_TRY(append(segments, timed(kBusBToX, trip.trip, trip.b, trip.x)));
        }

        MATHFP_TRY(append(segments, timed(kTrainStationToX, 10, 25.0, 41.0)));
        MATHFP_TRY(append(segments, timed(kTrainStationToX, 11, 65.0, 81.0)));
        MATHFP_TRY(append(segments, timed(kTrainStationToX, 12, 105.0, 121.0)));

        return segments;
    }

    [[nodiscard]] mathfp::Expected<SplitParams> article_split_params() {
        return make_split_params(
              Dimless{ 1.0 }
            , Dimless{ 0.0 }
            , Dimless{ 0.0 }
            , PerceivedJourneyTimeWeights{
                  .in_vehicle_time = Dimless{ 1.0 }
                , .access_time = Dimless{ 1.0 }
                , .egress_time = Dimless{ 1.0 }
                , .transfer_walk_time = Dimless{ 3.0 }
                , .transfer_wait_time = Dimless{ 3.0 }
                , .transfer_count = Dimless{ 2.0 }
                , .volume_capacity_ratio = Dimless{ 0.0 }
              }
            , TemporalUtilityWeights{
                  .early_departure = Dimless{ 1.0 }
                , .late_departure = Dimless{ 1.0 }
              }
            , SplitChoiceModelConfig{
                  .model = SplitChoiceModel::BoxCox
                , .exponent = Dimless{ 2.0 }
              }
            , SplitImpedanceTransformConfig{
                  .boxcox_transform_enabled = false
                , .boxcox_t = Dimless{ 0.0 }
              }
            , SplitIndependenceConfig{
                  .enabled = true
                , .gamma = Dimless{ 0.5 }
                , .temporal_similarity_scale = Dimless{ 30.0 }
                , .higher_quality_scale = Dimless{ 30.0 }
                , .lower_quality_scale = Dimless{ 30.0 }
                , .higher_perceived_journey_time_scale = Dimless{ 30.0 }
                , .lower_perceived_journey_time_scale = Dimless{ 30.0 }
                , .higher_fare_scale = Dimless{ 1.0 }
                , .lower_fare_scale = Dimless{ 1.0 }
              }
        );
    }

    [[nodiscard]] mathfp::Expected<SearchParams> article_params(
        ChoiceTolerances choice_tolerances
    ) {
        MATHFP_TRY_LET(
              SearchImpedance
            , impedance
            , make_search_impedance(
                  Dimless{ 1.0 }
                , Dimless{ 1.0 }
                , Dimless{ 1.0 }
                , Dimless{ 1.0 }
                , Dimless{ 1.0 }
                , Dimless{ 15.0 }
                , Dimless{ 0.0 }
                , FareNormalization{ .kind = FareNormalization::Kind::None }
              )
        );
        MATHFP_TRY_LET(
              TransferLimits
            , transfers
            , make_transfer_limits(
                  TransferCount{ 1 }
                , Time{ 0.0 }
                , Time{ 20.0 }
                , true
                , true
              )
        );
        MATHFP_TRY_LET(
              SearchTolerances
            , search_tolerances
            , make_search_tolerances(
                  Dimless{ 10.0 }
                , Dimless{ 1000.0 }
                , Dimless{ 10.0 }
                , Time{ 1000.0 }
                , Dimless{ 10.0 }
                , Dimless{ 10.0 }
              )
        );
        MATHFP_TRY_LET(SplitParams, split, article_split_params());
        return make_search_params(
              PreprocessParams{}
            , impedance
            , transfers
            , search_tolerances
            , choice_tolerances
            , split
        );
    }

    [[nodiscard]] mathfp::Expected<ChoiceTolerances> wide_choice_tolerances() {
        return make_choice_tolerances(
              Dimless{ 10.0 }
            , Dimless{ 1000.0 }
            , Dimless{ 10.0 }
            , Time{ 1000.0 }
            , Dimless{ 10.0 }
            , Dimless{ 10.0 }
        );
    }

    [[nodiscard]] mathfp::Expected<ChoiceTolerances> strict_journey_time_choice_tolerances() {
        return make_choice_tolerances(
              Dimless{ 10.0 }
            , Dimless{ 1000.0 }
            , Dimless{ 1.2 }
            , Time{ 0.0 }
            , Dimless{ 1.0 }
            , Dimless{ 1.0 }
        );
    }

    [[nodiscard]] InputModel article_input_model() {
        return InputModel{
              .stops = {
                  Stop{ .id = StopId{ kStopA } },
                  Stop{ .id = StopId{ kStation } },
                  Stop{ .id = StopId{ kStopB } },
                  Stop{ .id = StopId{ kStopX } }
              }
            , .zones = {
                  Zone{ .id = ZoneId{ kOrigin } },
                  Zone{ .id = ZoneId{ kDestination } }
              }
            , .intervals = {
                  TimeInterval{
                        .id = IntervalId{ kInterval }
                      , .start = Time{ 0.0 }
                      , .end = Time{ 140.0 }
                  }
              }
            , .demand = {
                  DemandEntry{
                        .origin = ZoneId{ kOrigin }
                      , .destination = ZoneId{ kDestination }
                      , .interval = IntervalId{ kInterval }
                      , .passengers = kDemand
                  }
              }
        };
    }

    [[nodiscard]] mathfp::Expected<ArticleScenario> article_scenario() {
        MATHFP_TRY_LET(std::vector<RouteSegment>, routes, article_route_segments());
        MATHFP_TRY_LET(
              std::vector<ConnectionSegment>
            , segments
            , article_connection_segments(routes)
        );
        MATHFP_TRY_LET(
              PreprocessedNetwork
            , network
            , build_preprocessed_network_from_segments(std::move(routes), std::move(segments))
        );
        MATHFP_TRY_LET(ChoiceTolerances, choice_tolerances, wide_choice_tolerances());
        MATHFP_TRY_LET(SearchParams, params, article_params(choice_tolerances));
        MATHFP_TRY_LET(
              SearchCostContext
            , search_cost
            , make_base_search_cost_context(params.impedance, compute_fare_scale(
                  network.connection_segments
                , params.impedance.fare_normalization
              ))
        );
        MATHFP_TRY_LET(
              SearchPruningExecutionPlan
            , pruning
            , plan_search_pruning_execution(
                  SearchPruningModelConfig{}
                , SearchPruningRolloutStage::ExactAndApproximateCurrentState
                , params.search_tolerances
              )
        );

        auto input = article_input_model();
        MATHFP_TRY_LET(std::vector<SearchTask>, tasks, build_search_tasks(input));
        MATHFP_TRY_LET(
              SearchTimeDomain
            , global_domain
            , make_search_time_domain({
                  SearchTimeWindow{
                        .begin = Time{ 0.0 }
                      , .end = Time{ 140.0 }
                  }
              })
        );

        return ArticleScenario{
              .input = std::move(input)
            , .network = std::move(network)
            , .params = std::move(params)
            , .search_cost = std::move(search_cost)
            , .pruning = std::move(pruning)
            , .tasks = std::move(tasks)
            , .time_domain_execution = SearchTimeDomainExecution{
                  .source_mode = SearchWindowMode::Global
                , .adaptation = SearchTimeDomainAdaptation::Strict
                , .padding = SearchTimePadding{}
                , .global_domain = std::move(global_domain)
              }
            , .assignment_period = AssignmentPeriodConfig{}
            , .admissibility = ConnectionAdmissibilityConfig{}
        };
    }

    [[nodiscard]] SearchExecutionRequest article_search_execution(
        const ArticleScenario& scenario
    ) {
        auto config = make_timed_connection_diagnostics_search_execution_config();
        config.validate_phase_invariants = true;
        return SearchExecutionRequest{
              .config = config
            , .time_domain_execution = std::cref(scenario.time_domain_execution)
            , .declared_zones = std::span<const Zone>{
                  scenario.input.zones.data()
                , scenario.input.zones.size()
              }
        };
    }

    [[nodiscard]] mathfp::Expected<ConnectionSearchResult> run_article_search(
        const ArticleScenario& scenario
    ) {
        return search_connections_branch_and_bound(
            BranchAndBoundSearchRequest{
                  .network = scenario.network
                , .tasks = std::span<const SearchTask>{
                      scenario.tasks.data()
                    , scenario.tasks.size()
                  }
                , .execution = article_search_execution(scenario)
                , .params = scenario.params
                , .search_cost = scenario.search_cost
                , .choice_config = ChoiceConfig{
                      .rollout_stage = ChoiceRolloutStage::ExactOnly
                  }
                , .assignment_period = scenario.assignment_period
                , .admissibility_config = scenario.admissibility
                , .pruning_execution = std::cref(scenario.pruning)
                , .complete_connection_dominance =
                    CompleteConnectionDominanceConfig{}
                , .diagnostics = SearchDiagnosticsContext{
                      .declared_zone_count = scenario.input.zones.size()
                    , .validate_phase_invariants = true
                  }
            }
        );
    }

    [[nodiscard]] std::vector<ExpectedConnection> connection_summary(
        std::span<const SearchConnection> connections
    ) {
        std::vector<ExpectedConnection> summary;
        summary.reserve(connections.size());
        for (const auto& connection : connections) {
            const auto metrics = metrics_of(connection);
            summary.push_back(
                ExpectedConnection{
                      .departure = metrics.departure_time.value()
                    , .arrival = metrics.arrival_time.value()
                    , .transfers = metrics.transfer_count.get()
                }
            );
        }
        std::sort(summary.begin(), summary.end());
        return summary;
    }

    [[nodiscard]] std::vector<ExpectedConnection> figure_one_connections() {
        std::vector<ExpectedConnection> expected{
              ExpectedConnection{ .departure = 10.0, .arrival = 41.0, .transfers = 1 }
            , ExpectedConnection{ .departure = 10.0, .arrival = 55.0, .transfers = 0 }
            , ExpectedConnection{ .departure = 55.0, .arrival = 100.0, .transfers = 0 }
            , ExpectedConnection{ .departure = 85.0, .arrival = 121.0, .transfers = 1 }
            , ExpectedConnection{ .departure = 85.0, .arrival = 130.0, .transfers = 0 }
        };
        std::sort(expected.begin(), expected.end());
        return expected;
    }

    [[nodiscard]] ExpectedConnection connection_key(
        const ConnectionMetrics& metrics
    ) noexcept {
        return ExpectedConnection{
              .departure = metrics.departure_time.value()
            , .arrival = metrics.arrival_time.value()
            , .transfers = metrics.transfer_count.get()
        };
    }

    [[nodiscard]] std::vector<ExpectedConnectionMetrics> figure_one_metric_oracle() {
        std::vector<ExpectedConnectionMetrics> expected{
              ExpectedConnectionMetrics{
                    .connection = ExpectedConnection{ .departure = 10.0, .arrival = 41.0, .transfers = 1 }
                  , .journey_time = 31.0
                  , .in_vehicle_time = 28.0
                  , .transfer_wait_time = 3.0
                  , .split_impedance = 39.0
              }
            , ExpectedConnectionMetrics{
                    .connection = ExpectedConnection{ .departure = 10.0, .arrival = 55.0, .transfers = 0 }
                  , .journey_time = 45.0
                  , .in_vehicle_time = 45.0
                  , .transfer_wait_time = 0.0
                  , .split_impedance = 45.0
              }
            , ExpectedConnectionMetrics{
                    .connection = ExpectedConnection{ .departure = 55.0, .arrival = 100.0, .transfers = 0 }
                  , .journey_time = 45.0
                  , .in_vehicle_time = 45.0
                  , .transfer_wait_time = 0.0
                  , .split_impedance = 45.0
              }
            , ExpectedConnectionMetrics{
                    .connection = ExpectedConnection{ .departure = 85.0, .arrival = 121.0, .transfers = 1 }
                  , .journey_time = 36.0
                  , .in_vehicle_time = 28.0
                  , .transfer_wait_time = 8.0
                  , .split_impedance = 54.0
              }
            , ExpectedConnectionMetrics{
                    .connection = ExpectedConnection{ .departure = 85.0, .arrival = 130.0, .transfers = 0 }
                  , .journey_time = 45.0
                  , .in_vehicle_time = 45.0
                  , .transfer_wait_time = 0.0
                  , .split_impedance = 45.0
              }
        };
        std::sort(expected.begin(), expected.end());
        return expected;
    }

    [[nodiscard]] std::vector<ExpectedConnectionMetrics> metric_summary(
        std::span<const SearchConnection> connections
    ) {
        std::vector<ExpectedConnectionMetrics> summary;
        summary.reserve(connections.size());
        for (const auto& connection : connections) {
            const auto metrics = metrics_of(connection);
            summary.push_back(
                ExpectedConnectionMetrics{
                      .connection = connection_key(metrics)
                    , .journey_time = metrics.journey_time.value()
                    , .in_vehicle_time = metrics.in_vehicle_time.value()
                    , .transfer_wait_time = metrics.transfer_wait_time.value()
                    , .split_impedance =
                          metrics.in_vehicle_time.value()
                        + 3.0 * metrics.transfer_wait_time.value()
                        + 2.0 * static_cast<double>(metrics.transfer_count.get())
                }
            );
        }
        std::sort(summary.begin(), summary.end());
        return summary;
    }

    [[nodiscard]] double figure_one_perceived_journey_time(
        const ConnectionMetrics& metrics
    ) noexcept {
        return
              metrics.in_vehicle_time.value()
            + metrics.access_time.value()
            + metrics.egress_time.value()
            + 3.0 * metrics.transfer_walk_time.value()
            + 3.0 * metrics.transfer_wait_time.value()
            + 2.0 * static_cast<double>(metrics.transfer_count.get());
    }

    [[nodiscard]] double oracle_temporal_similarity(
          const SplitOracleAlternative& lhs
        , const SplitOracleAlternative& rhs
    ) noexcept {
        return 0.5 * (
              std::abs(rhs.metrics.departure_time.value() - lhs.metrics.departure_time.value())
            + std::abs(rhs.metrics.arrival_time.value() - lhs.metrics.arrival_time.value())
        );
    }

    [[nodiscard]] double oracle_capped_proximity(
          double similarity
        , double scale
    ) noexcept {
        return 1.0 - std::min(1.0, similarity / scale);
    }

    [[nodiscard]] double oracle_connection_influence(
          const SplitOracleAlternative& base
        , const SplitOracleAlternative& other
    ) noexcept {
        constexpr auto gamma = 0.5;
        constexpr auto temporal_similarity_scale = 30.0;
        constexpr auto perceived_journey_time_scale = 30.0;

        const auto proximity = oracle_capped_proximity(
              oracle_temporal_similarity(base, other)
            , temporal_similarity_scale
        );
        const auto perceived_journey_time_delta =
            other.perceived_journey_time - base.perceived_journey_time;
        const auto quality_distance = std::min(
              1.0
            , std::abs(perceived_journey_time_delta) / perceived_journey_time_scale
        );
        return proximity * (1.0 - gamma * quality_distance);
    }

    [[nodiscard]] double oracle_independence(
          std::span<const SplitOracleAlternative> alternatives
        , std::size_t                            index
    ) noexcept {
        auto influence_sum = 0.0;
        for (std::size_t i = 0; i < alternatives.size(); ++i) {
            if (i == index) {
                continue;
            }
            influence_sum += oracle_connection_influence(alternatives[index], alternatives[i]);
        }
        return 1.0 / (1.0 + influence_sum);
    }

    [[nodiscard]] std::vector<SplitOracleAlternative> figure_one_split_oracle(
        std::span<const ConnectionDemandShare> shares
    ) {
        std::vector<SplitOracleAlternative> expected;
        expected.reserve(shares.size());
        for (const auto& share : shares) {
            const auto metrics = metrics_of(share.connection);
            expected.push_back(
                SplitOracleAlternative{
                      .key = connection_key(metrics)
                    , .metrics = metrics
                    , .perceived_journey_time = figure_one_perceived_journey_time(metrics)
                }
            );
        }
        std::sort(
              expected.begin()
            , expected.end()
            , [](const auto& lhs, const auto& rhs) {
                  return lhs.key < rhs.key;
              }
        );

        for (std::size_t i = 0; i < expected.size(); ++i) {
            expected[i].independence = oracle_independence(expected, i);
        }

        std::vector<double> log_weights;
        log_weights.reserve(expected.size());
        for (const auto& alternative : expected) {
            log_weights.push_back(
                  std::log(alternative.independence)
                - 2.0 * alternative.perceived_journey_time
            );
        }
        const auto max_log_weight = *std::max_element(log_weights.begin(), log_weights.end());
        auto normalizer = 0.0;
        for (const auto log_weight : log_weights) {
            normalizer += std::exp(log_weight - max_log_weight);
        }
        for (std::size_t i = 0; i < expected.size(); ++i) {
            expected[i].probability = std::exp(log_weights[i] - max_log_weight) / normalizer;
        }

        return expected;
    }

    [[nodiscard]] std::vector<SplitShareSummary> split_share_summary(
        std::span<const ConnectionDemandShare> shares
    ) {
        std::vector<SplitShareSummary> summary;
        summary.reserve(shares.size());
        for (const auto& share : shares) {
            summary.push_back(
                SplitShareSummary{
                      .key = connection_key(metrics_of(share.connection))
                    , .independence = share.independence
                    , .split_impedance = share.split_impedance
                    , .probability = share.probability
                    , .passengers = share.passengers
                }
            );
        }
        std::sort(summary.begin(), summary.end());
        return summary;
    }

    [[nodiscard]] double probability_sum(const DemandSplitResult& split) {
        return std::accumulate(
              split.shares.begin()
            , split.shares.end()
            , 0.0
            , [](double acc, const ConnectionDemandShare& share) {
                  return acc + share.probability;
              }
        );
    }

    [[nodiscard]] double passenger_sum(const DemandSplitResult& split) {
        return std::accumulate(
              split.shares.begin()
            , split.shares.end()
            , 0.0
            , [](double acc, const ConnectionDemandShare& share) {
                  return acc + share.passengers;
              }
        );
    }

}  // namespace

TEST(FriedrichHofsaessWekeckRegression, FigureOneTimedSearchChoiceAndSplit) {
    const auto scenario_result = article_scenario();
    ASSERT_TRUE(scenario_result.has_value()) << scenario_result.error().to_string();
    const auto& scenario = *scenario_result;

    const auto access = access_walk_connections_from(
          scenario.network.connection_index
        , endpoint_key(ZoneId{ kOrigin })
    );
    const auto first_boardings = collect_timed_connection_successors(
          scenario.network
        , endpoint_key(StopId{ kStopA })
        , std::nullopt
        , scenario.params.transfers
        , &scenario.tasks.front().departure_domain
    );
    const auto egress = egress_walk_connections_from(
          scenario.network.connection_index
        , endpoint_key(StopId{ kStopX })
    );
    EXPECT_EQ(access.size(), 1u);
    EXPECT_EQ(first_boardings.successors.size(), 9u);
    EXPECT_EQ(egress.size(), 1u);

    const auto residual_graph = build_residual_reverse_graph(
          std::span<const RouteSegment>{
              scenario.network.route_segments.data()
            , scenario.network.route_segments.size()
          }
        , std::span<const ConnectionSegment>{
              scenario.network.connection_segments.data()
            , scenario.network.connection_segments.size()
          }
    );
    const std::vector<SearchCompletionTarget> targets{
        SearchCompletionTarget{
              .index = SearchCompletionTargetRef{ 0 }
            , .destination = ZoneId{ kDestination }
        }
    };
    const auto reachability = build_residual_reachability(
          residual_graph
        , std::span<const SearchCompletionTarget>{ targets.data(), targets.size() }
        , scenario.params.transfers.max_transfers
        , scenario.params.impedance
        , scenario.search_cost.fare_scale
    );
    const auto root_reachability = evaluate_residual_reachability(
          reachability
        , RelaxedSuffixState{
              .current_physical = endpoint_key(ZoneId{ kOrigin })
            , .destination = ZoneId{ kDestination }
            , .phase = SearchBranchPhase::AtOrigin
            , .remaining_transfers = scenario.params.transfers.max_transfers
        }
        , scenario.params.transfers.max_transfers
    );
    EXPECT_TRUE(root_reachability.feasible);

    const auto search_result = run_article_search(scenario);
    ASSERT_TRUE(search_result.has_value()) << search_result.error().to_string();
    ASSERT_EQ(search_result->task_results.size(), 1u);

    const auto& task_connections = search_result->task_results.front().connections;
    EXPECT_EQ(connection_summary(task_connections), figure_one_connections());
    EXPECT_EQ(metric_summary(task_connections), figure_one_metric_oracle());

    const auto wide_choice = choose_connections(
          *search_result
        , scenario.params
        , scenario.search_cost
        , ChoiceConfig{ .rollout_stage = ChoiceRolloutStage::ExactAndApproximate }
        , scenario.assignment_period
        , scenario.admissibility
    );
    ASSERT_TRUE(wide_choice.has_value()) << wide_choice.error().to_string();
    ASSERT_EQ(wide_choice->task_results.size(), 1u);
    EXPECT_EQ(connection_summary(wide_choice->task_results.front().connections), figure_one_connections());

    const auto strict_choice_tolerances = strict_journey_time_choice_tolerances();
    ASSERT_TRUE(strict_choice_tolerances.has_value()) << strict_choice_tolerances.error().to_string();
    const auto strict_params = article_params(*strict_choice_tolerances);
    ASSERT_TRUE(strict_params.has_value()) << strict_params.error().to_string();
    const auto strict_choice = choose_connections(
          *search_result
        , *strict_params
        , scenario.search_cost
        , ChoiceConfig{ .rollout_stage = ChoiceRolloutStage::ExactAndApproximate }
        , scenario.assignment_period
        , scenario.admissibility
    );
    ASSERT_TRUE(strict_choice.has_value()) << strict_choice.error().to_string();
    ASSERT_EQ(strict_choice->task_results.size(), 1u);
    EXPECT_EQ(
          connection_summary(strict_choice->task_results.front().connections)
        , (std::vector<ExpectedConnection>{
              ExpectedConnection{ .departure = 10.0, .arrival = 41.0, .transfers = 1 }
            , ExpectedConnection{ .departure = 85.0, .arrival = 121.0, .transfers = 1 }
          })
    );

    const auto split = split_demand_over_connections(
          *wide_choice
        , scenario.input
        , scenario.params
        , DemandSegmentTimeConfig{}
    );
    ASSERT_TRUE(split.has_value()) << split.error().to_string();

    ASSERT_EQ(split->shares.size(), 5u);
    EXPECT_TRUE(split->unassigned.empty());
    EXPECT_NEAR(probability_sum(*split), 1.0, 1e-9);
    EXPECT_NEAR(passenger_sum(*split), kDemand, 1e-7);
    for (const auto& share : split->shares) {
        EXPECT_GT(share.probability, 0.0);
        EXPECT_GT(share.passengers, 0.0);
        EXPECT_TRUE(std::isfinite(share.independence));
        EXPECT_GT(share.independence, 0.0);
        EXPECT_TRUE(std::isfinite(share.split_impedance));
    }

    const auto actual_split = split_share_summary(split->shares);
    const auto expected_split = figure_one_split_oracle(split->shares);
    ASSERT_EQ(actual_split.size(), expected_split.size());
    for (std::size_t i = 0; i < actual_split.size(); ++i) {
        EXPECT_EQ(actual_split[i].key, expected_split[i].key);
        EXPECT_NEAR(
              actual_split[i].split_impedance
            , expected_split[i].perceived_journey_time
            , 1e-12
        );
        EXPECT_NEAR(actual_split[i].independence, expected_split[i].independence, 1e-12);
        EXPECT_NEAR(actual_split[i].probability, expected_split[i].probability, 1e-12);
        EXPECT_NEAR(actual_split[i].passengers, kDemand * expected_split[i].probability, 1e-9);
    }
}

}  // namespace timetable::domain::assignment
