#include "timetable/domain/assignment/split/split.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "../detail/grouping.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
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

        struct SplitAlternative final {
            SearchConnection  connection;
            ConnectionMetrics metrics{};
            double            perceived_journey_time{};
            double            independence{};
        };

        std::size_t max_weight_index(
            std::span<const double> weights
        ) noexcept {
            std::size_t index = 0;
            for (std::size_t i = 1; i < weights.size(); ++i) {
                if (weights[i] > weights[index]) {
                    index = i;
                }
            }
            return index;
        }

        bool is_non_negative_roundoff(
              double value
            , double scale
        ) noexcept {
            return value >= 0.0
                || mathfp::almost_equal(
                      value
                    , 0.0
                    , mathfp::abs_tolerance(scale)
                    , mathfp::rel_tolerance_coeff<double>()
                );
        }

        double split_support_probability_tolerance() noexcept {
            return mathfp::abs_tolerance(1.0);
        }

        double split_support_passenger_tolerance(
            double demand_passengers
        ) noexcept {
            return mathfp::abs_tolerance(demand_passengers);
        }

        bool is_numerically_significant_share(
              double probability
            , double passengers
            , double demand_passengers
        ) noexcept {
            return probability > split_support_probability_tolerance()
                && passengers  > split_support_passenger_tolerance(demand_passengers);
        }

        std::size_t compact_numerical_support(
              std::vector<double>& probabilities
            , std::vector<double>& passengers
            , std::size_t          residual_index
            , double               demand_passengers
        ) noexcept {
            mathfp::CompensatedSum<double> suppressed_probability;
            mathfp::CompensatedSum<double> suppressed_passengers;
            std::size_t suppressed_count = 0;

            // Values below floating-point resolution do not form a meaningful
            // numerical support; move their mass to the residual alternative so
            // the reported split still conserves probability and passengers.
            for (std::size_t i = 0; i < probabilities.size(); ++i) {
                if (i == residual_index) {
                    continue;
                }
                if (is_numerically_significant_share(
                      probabilities[i]
                    , passengers[i]
                    , demand_passengers
                )) {
                    continue;
                }

                suppressed_probability.add(probabilities[i]);
                suppressed_passengers .add(passengers[i]);
                probabilities[i] = 0.0;
                passengers[i]    = 0.0;
                ++suppressed_count;
            }

            probabilities[residual_index] += suppressed_probability.value();
            passengers   [residual_index] += suppressed_passengers .value();
            return suppressed_count;
        }

        double perceived_journey_time(
              const ConnectionMetrics&           metrics
            , const PerceivedJourneyTimeWeights& weights
        ) noexcept {
            return
                  weighted_duration(metrics.in_vehicle_time   , weights.in_vehicle_time)
                + weighted_duration(metrics.access_time       , weights.access_time)
                + weighted_duration(metrics.egress_time       , weights.egress_time)
                + weighted_duration(metrics.transfer_walk_time, weights.transfer_walk_time)
                + weighted_duration(metrics.transfer_wait_time, weights.transfer_wait_time)
                + weighted_transfer_count(metrics.transfer_count, weights.transfer_count);
        }

        Time split_reference_time(
              const ConnectionMetrics& metrics
            , DemandSegmentBasis       basis
        ) noexcept {
            switch (basis) {
                case DemandSegmentBasis::Departure:
                    return metrics.departure_time;
                case DemandSegmentBasis::Arrival:
                    return metrics.arrival_time;
            }
            return metrics.departure_time;
        }

        double split_early_deviation(
              Time                reference_time
            , const TimeInterval& interval
        ) noexcept {
            return std::max(
                  0.0
                , interval.start.value() - reference_time.value()
            );
        }

        double split_late_deviation(
              Time                reference_time
            , const TimeInterval& interval
        ) noexcept {
            return std::max(
                  0.0
                , reference_time.value() - interval.end.value()
            );
        }

        double temporal_utility(
              const SplitAlternative&       alternative
            , const TimeInterval&           interval
            , DemandSegmentBasis            basis
            , const TemporalUtilityWeights& weights
        ) noexcept {
            const auto reference_time = split_reference_time(
                  alternative.metrics
                , basis
            );
            return
                  mathfp::units::as_dimless(weights.early_departure)
                    * split_early_deviation(reference_time, interval)
                + mathfp::units::as_dimless(weights.late_departure)
                    * split_late_deviation(reference_time, interval);
        }

        double split_impedance(
              const SplitAlternative& alternative
            , const TimeInterval&     interval
            , const SplitParams&      params
            , DemandSegmentBasis      basis
        ) noexcept {
            return
                  mathfp::units::as_dimless(params.q_time)
                    * alternative.perceived_journey_time
                + mathfp::units::as_dimless(params.q_departure)
                    * temporal_utility(
                          alternative
                        , interval
                        , basis
                        , params.temporal_utility
                    )
                + mathfp::units::as_dimless(params.q_fare)
                    * alternative.metrics.fare;
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

        double positive_log_argument(
            double value
        ) noexcept {
            return std::max(value, numeric::positive_stability_floor());
        }

        double log_independence_weight(
            double independence
        ) noexcept {
            return std::log(positive_log_argument(independence));
        }

        double kirchhoff_log_weight(
              double exponent
            , double impedance
            , double independence
        ) noexcept {
            return log_independence_weight(independence)
                - exponent * std::log(positive_log_argument(impedance));
        }

        double logit_log_weight(
              double exponent
            , double impedance
            , double independence
        ) noexcept {
            return log_independence_weight(independence)
                - exponent * impedance;
        }

        double boxcox_log_weight(
              double exponent
            , double boxcox_t
            , double impedance
            , double independence
        ) noexcept {
            return log_independence_weight(independence)
                - exponent * box_cox_transform(impedance, boxcox_t);
        }

        mathfp::Expected<mathfp::Unit> validate_supported_split_choice_model(
            SplitChoiceModel model
        ) {
            if (model == SplitChoiceModel::Lohse) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("Lohse split choice model is not implemented")
                        .ctx("choice_model", std::string(to_string(model)))
                );
            }
            return mathfp::kUnit;
        }

        mathfp::Expected<double> split_choice_log_weight(
              const SplitChoiceModelConfig& model
            , double                        impedance
            , double                        independence
        ) {
            const auto exponent = mathfp::units::as_dimless(model.exponent);
            switch (model.model) {
                case SplitChoiceModel::Kirchhoff:
                    return kirchhoff_log_weight(exponent, impedance, independence);

                case SplitChoiceModel::Logit:
                    return logit_log_weight(exponent, impedance, independence);

                case SplitChoiceModel::BoxCox:
                    return boxcox_log_weight(
                          exponent
                        , mathfp::units::as_dimless(model.boxcox_t)
                        , impedance
                        , independence
                    );

                case SplitChoiceModel::Lohse:
                    return mathfp::unexpected(
                        mathfp::invalid_arg("Lohse split choice model is not implemented")
                            .ctx("choice_model", std::string(to_string(model.model)))
                    );
            }

            return mathfp::unexpected(
                mathfp::invalid_arg("unsupported split choice model")
                    .ctx("choice_model", static_cast<std::int64_t>(model.model))
            );
        }

        double temporal_similarity(
              const SplitAlternative& lhs
            , const SplitAlternative& rhs
        ) noexcept {
            return
                0.5
                * (
                std::abs(rhs.metrics.departure_time.value() - lhs.metrics.departure_time.value())
                + std::abs(rhs.metrics.arrival_time.value() - lhs.metrics.arrival_time.value())
            );
        }

        double base_journey_quality_advantage(
              const SplitAlternative& lhs
            , const SplitAlternative& rhs
        ) noexcept {
            return rhs.perceived_journey_time - lhs.perceived_journey_time;
        }

        double base_fare_quality_advantage(
              const SplitAlternative& lhs
            , const SplitAlternative& rhs
        ) noexcept {
            return rhs.metrics.fare - lhs.metrics.fare;
        }

        bool base_connection_is_superior(
            double base_quality_advantage
        ) noexcept {
            return base_quality_advantage >= 0.0;
        }

        double asymmetric_quality_scale(
              double             base_quality_advantage
            , const SplitIndependenceConfig& config
        ) noexcept {
            return base_connection_is_superior(base_quality_advantage)
                ? mathfp::units::as_dimless(config.higher_quality_scale)
                : mathfp::units::as_dimless(config.lower_quality_scale);
        }

        double normalized_quality_distance(
              double             base_quality_advantage
            , const SplitIndependenceConfig& config
        ) noexcept {
            const auto scale = asymmetric_quality_scale(base_quality_advantage, config);
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
              const SplitAlternative& base
            , const SplitAlternative& other
            , const SplitIndependenceConfig& config
        ) noexcept {
            const auto x = temporal_similarity(base, other);
            const auto y = base_journey_quality_advantage(base, other);
            const auto z = base_fare_quality_advantage(base, other);
            const auto proximity = capped_proximity(
                  x
                , mathfp::units::as_dimless(config.temporal_similarity_scale)
            );
            const auto y_term = normalized_quality_distance(y, config);
            const auto z_term = normalized_quality_distance(z, config);

            return proximity * std::exp(
                -mathfp::units::as_dimless(config.gamma) * (y_term + z_term)
            );
        }

        double split_independence(
              const SplitIndependenceConfig&    config
            , std::span<const SplitAlternative> alternatives
            , std::size_t                       index
        ) noexcept {
            if (!config.enabled) {
                return 1.0;
            }

            mathfp::CompensatedSum<double> influence_sum;
            for (std::size_t i = 0; i < alternatives.size(); ++i) {
                if (i == index) {
                    continue;
                }
                influence_sum.add(connection_influence(
                      alternatives[index]
                    , alternatives[i]
                    , config
                ));
            }
            return 1.0 / (1.0 + influence_sum.value());
        }

        std::vector<SplitAlternative> derive_split_alternatives(
              const std::vector<const SearchConnection*>& connections
            , const SplitParams&            params
        ) {
            std::vector<SplitAlternative> alternatives;
            alternatives.reserve(connections.size());

            for (const auto* connection : connections) {
                const auto metrics = metrics_of(*connection);
                alternatives.push_back(
                    SplitAlternative{
                          .connection             = *connection
                        , .metrics                = metrics
                        , .perceived_journey_time = perceived_journey_time(
                              metrics
                            , params.perceived_journey_time
                          )
                        , .independence           = 0.0
                    }
                );
            }

            for (std::size_t i = 0; i < alternatives.size(); ++i) {
                alternatives[i].independence = split_independence(
                      params.independence
                    , alternatives
                    , i
                );
            }

            return alternatives;
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

        using ChoiceTaskLookup = std::map<detail::grouping::DemandKey, const ChoiceTaskResult*>;

        mathfp::Expected<ChoiceTaskLookup> build_choice_task_lookup(
            const ConnectionChoiceResult& choice_result
        ) {
            ChoiceTaskLookup lookup;
            for (const auto& task_result : choice_result.task_results) {
                const auto key = detail::grouping::DemandKey{
                      .origin      = task_result.task.origin
                    , .destination = task_result.task.destination
                    , .interval    = task_result.task.interval.id
                };
                if (!lookup.emplace(key, &task_result).second) {
                    return mathfp::unexpected(
                        mathfp::internal_error("choice result contains duplicate task result for demand key")
                            .ctx("origin"     , key.origin     .get())
                            .ctx("destination", key.destination.get())
                            .ctx("interval_id", key.interval   .get())
                    );
                }
            }
            return lookup;
        }

    }  // namespace

    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
        , const DemandSegmentTimeConfig& demand_segment_time
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        MATHFP_TRY(validate_supported_split_choice_model(params.split.choice_model.model));
        MATHFP_TRY(validate_demand_segment_time_config(demand_segment_time));

        both("split: demand assignment");
        log(
            fmt::format(
                  "split input: chosen_connections = {:>8}  choice_tasks = {:>8}  demand_entries = {:>8}  choice_model = {}  demand_basis = {}"
                , choice_result.connections.size()
                , choice_result.task_results.size()
                , input.demand.size()
                , to_string(params.split.choice_model.model)
                , to_string(demand_segment_time.basis)
            )
            , LogLevel::Info
        );

        DemandSplitResult result;
        MATHFP_TRY_LET(
              ChoiceTaskLookup
            , task_lookup
            , build_choice_task_lookup(choice_result)
        );
        const auto interval_lookup    = build_interval_lookup(input);
        std::size_t suppressed_numerical_shares = 0;

        for (const auto& demand : input.demand) {
            if (demand.passengers <= 0.0) {
                continue;
            }

            const auto demand_key = detail::grouping::demand_key(demand);
            const auto task_it = task_lookup.find(demand_key);
            if (task_it == task_lookup.end()) {
                return mathfp::unexpected(
                    mathfp::internal_error("positive demand entry has no matching choice task")
                        .ctx("origin"     , demand.origin     .get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval   .get())
                );
            }
            const auto& task_result = *task_it->second;
            if (task_result.connections.empty()) {
                continue;
            }

            if (task_result.task.origin != demand.origin
                || task_result.task.destination != demand.destination
                || task_result.task.interval.id != demand.interval) {
                return mathfp::unexpected(
                    mathfp::internal_error("choice task metadata does not match demand entry")
                        .ctx("origin"     , demand.origin     .get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval_id", demand.interval   .get())
                );
            }

            const auto interval = find_interval(interval_lookup, demand.interval);
            if (!interval) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("demand entry references unknown time interval")
                        .ctx("interval_id", demand.interval.get())
                );
            }

            std::vector<const SearchConnection*> task_connections;
            task_connections.reserve(task_result.connections.size());
            for (const auto& connection : task_result.connections) {
                task_connections.push_back(&connection);
            }
            const auto alternatives = derive_split_alternatives(
                  task_connections
                , params.split
            );
            std::vector<double> independences;
            std::vector<double> split_impedances;
            std::vector<double> log_weights;

            independences   .reserve(alternatives.size());
            split_impedances.reserve(alternatives.size());
            log_weights     .reserve(alternatives.size());

            double max_log_weight = -std::numeric_limits<double>::infinity();
            for (std::size_t i = 0; i < alternatives.size(); ++i) {
                const auto independence = alternatives[i].independence;
                const auto imp = split_impedance(
                      alternatives[i]
                    , *interval
                    , params.split
                    , demand_segment_time.basis
                );
                MATHFP_TRY_LET(double, log_weight, split_choice_log_weight(
                      params.split.choice_model
                    , imp
                    , independence
                ));

                independences   .push_back(independence);
                split_impedances.push_back(imp);
                log_weights     .push_back(log_weight);

                max_log_weight = std::max(max_log_weight, log_weight);
            }

            std::vector<double> weights;
            weights.reserve(log_weights.size());
            for (const auto log_weight : log_weights) {
                const auto weight = std::exp(log_weight - max_log_weight);
                weights.push_back(weight);
            }
            const auto weight_sum = mathfp::compensated_sum(weights);

            if (!(weight_sum > 0.0) || !std::isfinite(weight_sum)) {
                return mathfp::unexpected(
                    mathfp::domain_error("invalid split weight normalization")
                        .ctx("origin"     , demand.origin.get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval"   , task_result.task.interval.id.get())
                );
            }

            const auto residual_index = max_weight_index(weights);
            std::vector<double> probabilities(alternatives.size(), 0.0);
            std::vector<double> passengers(alternatives.size(), 0.0);

            mathfp::CompensatedSum<double> probability_prefix;
            mathfp::CompensatedSum<double> passenger_prefix;
            for (std::size_t i = 0; i < alternatives.size(); ++i) {
                if (i == residual_index) {
                    continue;
                }
                const auto probability = weights[i] / weight_sum;
                const auto passenger_count = demand.passengers * probability;
                probabilities[i] = probability;
                passengers[i] = passenger_count;
                probability_prefix.add(probability);
                passenger_prefix.add(passenger_count);
            }

            auto residual_probability = 1.0 - probability_prefix.value();
            auto residual_passengers = demand.passengers - passenger_prefix.value();
            if (!is_non_negative_roundoff(residual_probability, 1.0)
                || !is_non_negative_roundoff(residual_passengers, demand.passengers)) {
                return mathfp::unexpected(
                    mathfp::domain_error("invalid residual split normalization")
                        .ctx("origin"              , demand.origin.get())
                        .ctx("destination"         , demand.destination.get())
                        .ctx("interval"            , task_result.task.interval.id.get())
                        .ctx("residual_probability", residual_probability)
                        .ctx("residual_passengers" , residual_passengers)
                );
            }
            residual_probability = std::max(0.0, residual_probability);
            residual_passengers = std::max(0.0, residual_passengers);
            probabilities[residual_index] = residual_probability;
            passengers[residual_index] = residual_passengers;

            suppressed_numerical_shares += compact_numerical_support(
                  probabilities
                , passengers
                , residual_index
                , demand.passengers
            );

            for (std::size_t i = 0; i < alternatives.size(); ++i) {
                if (!(probabilities[i] > 0.0) && !(passengers[i] > 0.0)) {
                    continue;
                }
                result.shares.push_back(
                    ConnectionDemandShare{
                          .origin          = demand.origin
                        , .destination     = demand.destination
                        , .interval        = demand.interval
                        , .connection      = alternatives[i].connection
                        , .passengers      = passengers[i]
                        , .probability     = probabilities[i]
                        , .independence    = independences[i]
                        , .split_impedance = split_impedances[i]
                    }
                );
            }
        }

        log(
            fmt::format(
                  "split result: shares = {:>8}  suppressed_numerical_shares = {:>8}"
                , result.shares.size()
                , suppressed_numerical_shares
            )
            , LogLevel::Info
        );
        both("split: demand assignment done");
        return result;
    }

}  // namespace timetable::domain::assignment
