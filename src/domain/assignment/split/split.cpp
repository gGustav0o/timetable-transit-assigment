#include "timetable/domain/assignment/split/split.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <span>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "../detail/grouping.hpp"
#include "timetable/domain/numeric.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        double transfer_count_value(
            TransferCount transfers
        ) noexcept {
            return static_cast<double>(transfers.get());
        }

        double weighted_duration(
              Time    duration
            , Dimless weight
        ) noexcept {
            return mathfp::units::as_dimless(weight) * duration.value();
        }

        double weighted_transfer_count(
              TransferCount transfers
            , Dimless       weight
        ) noexcept {
            return mathfp::units::as_dimless(weight) * transfer_count_value(transfers);
        }

        double perceived_journey_time(
              const DiscoveredConnection&        connection
            , const PerceivedJourneyTimeWeights& weights
        ) noexcept {
            return weighted_duration(connection.journey_time, weights.journey_time)
                + weighted_duration(connection.transfer_time, weights.transfer_time)
                + weighted_transfer_count(connection.transfers, weights.transfer_count);
        }

        double early_departure_deviation(
              const DiscoveredConnection& connection
            , const TimeInterval&         interval
        ) noexcept {
            return std::max(0.0, interval.start.value() - connection.departure.value());
        }

        double late_departure_deviation(
              const DiscoveredConnection& connection
            , const TimeInterval&         interval
        ) noexcept {
            return std::max(0.0, connection.departure.value() - interval.end.value());
        }

        double temporal_utility(
              const DiscoveredConnection&   connection
            , const TimeInterval&           interval
            , const TemporalUtilityWeights& weights
        ) noexcept {
            return mathfp::units::as_dimless(weights.early_departure)
                    * early_departure_deviation(connection, interval)
                + mathfp::units::as_dimless(weights.late_departure)
                    * late_departure_deviation(connection, interval);
        }

        double split_impedance(
              const DiscoveredConnection& connection
            , const TimeInterval&         interval
            , const SplitParams&          params
        ) noexcept {
            return mathfp::units::as_dimless(params.q_time)
                    * perceived_journey_time(connection, params.perceived_journey_time)
                + mathfp::units::as_dimless(params.q_departure)
                    * temporal_utility(connection, interval, params.temporal_utility)
                + mathfp::units::as_dimless(params.q_fare) * connection.fare;
        }

        double box_cox_transform(
              double value
            , double t
        ) noexcept {
            const auto positive = std::max(value, numeric::positive_stability_floor());
            if (mathfp::almost_zero(t)) {
                return std::log(positive);
            }
            return (std::pow(positive, t) - 1.0) / t;
        }

        double temporal_similarity(
              const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
        ) noexcept {
            return 0.5 * (
                std::abs(rhs.departure.value() - lhs.departure.value())
                + std::abs(rhs.arrival.value() - lhs.arrival.value())
            );
        }

        double base_journey_quality_advantage(
              const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
            , const SplitParams&          params
        ) noexcept {
            return perceived_journey_time(rhs, params.perceived_journey_time)
                - perceived_journey_time(lhs, params.perceived_journey_time);
        }

        double base_fare_quality_advantage(
              const DiscoveredConnection& lhs
            , const DiscoveredConnection& rhs
        ) noexcept {
            return rhs.fare - lhs.fare;
        }

        bool base_connection_is_superior(
            double base_quality_advantage
        ) noexcept {
            return base_quality_advantage >= 0.0;
        }

        double asymmetric_quality_scale(
              double             base_quality_advantage
            , const SplitParams& params
        ) noexcept {
            return base_connection_is_superior(base_quality_advantage)
                ? mathfp::units::as_dimless(params.higher_quality_scale)
                : mathfp::units::as_dimless(params.lower_quality_scale);
        }

        double normalized_quality_distance(
              double             base_quality_advantage
            , const SplitParams& params
        ) noexcept {
            const auto scale = asymmetric_quality_scale(base_quality_advantage, params);
            if (scale <= 0.0) {
                return 0.0;
            }
            return std::abs(base_quality_advantage) / scale;
        }

        double capped_proximity(
              double similarity
            , double scale
        ) noexcept {
            if (scale <= 0.0) {
                return similarity <= 0.0 ? 1.0 : 0.0;
            }
            return 1.0 - std::min(1.0, similarity / scale);
        }

        double connection_influence(
              const DiscoveredConnection& base
            , const DiscoveredConnection& other
            , const SplitParams&          params
        ) noexcept {
            const auto x = temporal_similarity(base, other);
            const auto y = base_journey_quality_advantage(base, other, params);
            const auto z = base_fare_quality_advantage(base, other);
            const auto proximity = capped_proximity(
                  x
                , mathfp::units::as_dimless(params.temporal_similarity_scale)
            );
            const auto y_term = normalized_quality_distance(y, params);
            const auto z_term = normalized_quality_distance(z, params);

            return proximity * std::exp(-mathfp::units::as_dimless(params.gamma) * (y_term + z_term));
        }

        double connection_independence(
              std::span<const DiscoveredConnection> connections
            , std::size_t                           index
            , const SplitParams&                    params
        ) noexcept {
            double influence_sum = 0.0;
            for (std::size_t i = 0; i < connections.size(); ++i) {
                if (i == index) {
                    continue;
                }
                influence_sum += connection_influence(connections[index], connections[i], params);
            }
            return 1.0 / (1.0 + influence_sum);
        }

        using IntervalLookup = std::map<IntervalId, const TimeInterval*>;

        IntervalLookup build_interval_lookup(
            const InputModel& input
        ) {
            IntervalLookup lookup;
            for (const auto& interval : input.intervals) {
                lookup.emplace(interval.id, &interval);
            }
            return lookup;
        }

        const TimeInterval* find_interval(
              const IntervalLookup& lookup
            , IntervalId             id
        ) noexcept {
            const auto it = lookup.find(id);
            return it == lookup.end() ? nullptr : it->second;
        }

    }  // namespace

    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        both("split: demand assignment");
        log(
            fmt::format(
                  "split input: chosen_connections = {:>8}  demand_entries = {:>8}"
                , choice_result.connections.size()
                , input.demand.size()
            )
            , LogLevel::Info
        );

        DemandSplitResult result;
        const auto groups          = detail::grouping::group_connections_by_od(choice_result.connections);
        const auto interval_lookup = build_interval_lookup(input);
        const auto beta            = mathfp::units::as_dimless(params.split.beta);
        const auto boxcox_t        = mathfp::units::as_dimless(params.split.boxcox_t);

        for (const auto& demand : input.demand) {
            const auto interval = find_interval(interval_lookup, demand.interval);
            if (!interval) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("demand entry references unknown time interval")
                        .ctx("interval_id", demand.interval.get())
                );
            }

            const auto it = groups.find(detail::grouping::OdKey{ demand.origin, demand.destination });
            if (it == groups.end() || it->second.empty() || demand.passengers <= 0.0) {
                continue;
            }

            const auto& connections = it->second;
            std::vector<double> independences;
            std::vector<double> split_impedances;
            std::vector<double> log_weights;

            independences   .reserve(connections.size());
            split_impedances.reserve(connections.size());
            log_weights     .reserve(connections.size());

            double max_log_weight = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < connections.size(); ++i) {
                const auto independence = connection_independence(
                      connections
                    , i
                    , params.split
                );
                const auto imp = split_impedance(
                      connections[i]
                    , *interval
                    , params.split
                );
                const auto transformed = box_cox_transform(imp, boxcox_t);
                const auto log_weight = std::log(std::max(
                      independence
                    , numeric::positive_stability_floor()
                ))
                    - beta * transformed;

                independences   .push_back(independence);
                split_impedances.push_back(imp);
                log_weights     .push_back(log_weight);

                max_log_weight = std::max(max_log_weight, log_weight);
            }

            double weight_sum = 0.0;
            std::vector<double> weights;
            weights.reserve(log_weights.size());
            for (const auto log_weight : log_weights) {
                const auto weight = std::exp(log_weight - max_log_weight);
                weights.push_back(weight);
                weight_sum += weight;
            }

            if (!(weight_sum > 0.0) || !std::isfinite(weight_sum)) {
                return mathfp::unexpected(
                    mathfp::domain_error("invalid split weight normalization")
                        .ctx("origin"     , demand.origin.get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval"   , demand.interval.get())
                );
            }

            for (std::size_t i = 0; i < connections.size(); ++i) {
                const auto probability = weights[i] / weight_sum;
                result.shares.push_back(
                    ConnectionDemandShare{
                          .origin          = demand.origin
                        , .destination     = demand.destination
                        , .interval        = demand.interval
                        , .connection      = connections[i]
                        , .passengers      = demand.passengers * probability
                        , .probability     = probability
                        , .independence    = independences[i]
                        , .split_impedance = split_impedances[i]
                    }
                );
            }
        }

        log(
            fmt::format(
                  "split result: shares = {:>8}"
                , result.shares.size()
            )
            , LogLevel::Info
        );
        both("split: demand assignment done");
        return result;
    }

}  // namespace timetable::domain::assignment
