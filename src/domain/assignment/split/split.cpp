#include "timetable/domain/assignment/split/split.hpp"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include <fmt/format.h>

#include "../detail/grouping.hpp"
#include "timetable/domain/assignment/capacity_aware_assignment.hpp"
#include "timetable/domain/assignment/connection_admissibility.hpp"
#include "timetable/domain/numeric.hpp"
#include "timetable/infra/progress_bus.hpp"

namespace timetable::domain::assignment {
    namespace {

        struct SplitAlternative final {
            /*
             * Split alternatives are lazy views over retained supports. The
             * heavy SearchConnection/support payload is copied only when a
             * positive demand share is emitted.
             */
            const SearchConnection* connection{};
            const DayPathAlternative* day_path_alternative{};
            DemandShareAlternativeSource source{ DemandShareAlternativeSource::TimedConnection };
            DayPathSignature  day_path{};
            const DayPathSupportDescriptor* day_path_support{};
            ConnectionMetrics metrics{};
            double            perceived_journey_time{};
            double            independence{};
        };

        [[nodiscard]] const SearchConnection& connection_of(
            const SplitAlternative& alternative
        ) {
            if (alternative.source == DemandShareAlternativeSource::DayPath) {
                return day_path_representative_connection(*alternative.day_path_alternative);
            }
            return *alternative.connection;
        }

        [[nodiscard]] std::optional<DayPathSupportDescriptor> materialize_day_path_support(
            const SplitAlternative& alternative
        ) {
            if (alternative.day_path_support == nullptr) {
                return std::nullopt;
            }
            return *alternative.day_path_support;
        }

        /**
         * @brief One paper-level split alternative inside an OD-day path.
         *
         * DayPathAlternative is the structural, service-day identity retained
         * by search. SplitConnectionAlternative is the interval-admissible
         * timed connection support c in C(a) used by the paper split model.
         */
        struct SplitConnectionAlternative final {
            const DayPathAlternative*       path{};
            const DayPathSupportDescriptor* support{};
        };

        struct IntervalSplitConnectionAlternatives final {
            std::vector<SplitConnectionAlternative> alternatives{};
            std::size_t candidate_supports{};
            std::size_t rejected_supports{};
        };

        struct SplitCapacityContext final {
            const CapacityAwareAssignmentConfig* config{};
            const VehicleJourneyItemLoadState*   load_state{};
            const VehicleJourneyItemCapacitySet* capacity_set{};
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
            //tex:
            // Perceived journey time is user-defined. The paper's typical
            // example $$PJT(c)=JT(c)+2TT(c)+2NT(c)$$ is represented here by
            // configurable weights over ride, access/egress, transfer walk,
            // transfer wait and transfer count components.
            return
                  timetable::domain::weighted_duration(
                      metrics.in_vehicle_time
                    , weights.in_vehicle_time
                  )
                + timetable::domain::weighted_duration(metrics.access_time, weights.access_time)
                + timetable::domain::weighted_duration(metrics.egress_time, weights.egress_time)
                + timetable::domain::weighted_duration(
                      metrics.transfer_walk_time
                    , weights.transfer_walk_time
                  )
                + timetable::domain::weighted_duration(
                      metrics.transfer_wait_time
                    , weights.transfer_wait_time
                  )
                + timetable::domain::weighted_transfer_count(
                      metrics.transfer_count
                    , weights.transfer_count
                );
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
            //tex:
            // Temporal utility compares the chosen demand interval $$a$$ with
            // the realized departure or arrival reference time:
            // $$U_a(c)=u_e\max(0,start(a)-T(c))+u_l\max(0,T(c)-end(a)).$$
            // Hence $$U_a(c)=0$$ inside the interval and grows monotonically
            // outside it.
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
            //tex:
            // Interval-specific split impedance:
            // $$IMP_a(c)=q_1PJT(c)+q_2U_a(c)+q_3FARE(c).$$
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
            //tex:
            // Box--Cox transform from the paper:
            // $$b^{(t)}(IMP)=\begin{cases}(IMP^t-1)/t,&t\ne0,\\ \log(IMP),&t=0.\end{cases}$$
            // A positive numerical floor is applied only to keep logarithms and
            // powers well-defined for near-zero modeled impedances.
            const auto positive = std::max(value, numeric::positive_stability_floor());
            if (mathfp::almost_zero(t)) {
                return std::log(positive);
            }
            return (std::pow(positive, t) - 1.0) / t;
        }

        double transform_split_impedance(
              const SplitImpedanceTransformConfig& config
            , double                                impedance
        ) noexcept {
            const auto positive = std::max(
                  impedance
                , numeric::positive_stability_floor()
            );
            if (!config.boxcox_transform_enabled) {
                return positive;
            }
            return box_cox_transform(
                  positive
                , mathfp::units::as_dimless(config.boxcox_t)
            );
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

        double transformed_impedance_log_weight(
              double exponent
            , double impedance
            , double independence
        ) noexcept {
            return log_independence_weight(independence)
                - exponent * impedance;
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
            , double                        transformed_impedance
            , double                        independence
        ) {
            if (!std::isfinite(transformed_impedance)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split choice impedance must be finite")
                        .ctx("choice_model", std::string(to_string(model.model)))
                        .ctx("impedance", transformed_impedance)
                );
            }

            const auto exponent = mathfp::units::as_dimless(model.exponent);
            //tex:
            // The BoxCox branch implements the MNL weight from the paper in log
            // space:
            // $$\log w_a(c)=\log IND(c)-\beta b^{(t)}(IMP_a(c)).$$
            // The value passed here is already either raw $$IMP_a(c)$$ or the
            // transformed $$b^{(t)}(IMP_a(c))$$ according to SplitImpedanceTransformConfig.
            switch (model.model) {
                case SplitChoiceModel::Kirchhoff:
                    return kirchhoff_log_weight(
                          exponent
                        , transformed_impedance
                        , independence
                    );

                case SplitChoiceModel::Logit:
                    return logit_log_weight(
                          exponent
                        , transformed_impedance
                        , independence
                    );

                case SplitChoiceModel::BoxCox:
                    return transformed_impedance_log_weight(
                          exponent
                        , transformed_impedance
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
            //tex:
            // Temporal similarity term:
            // $$x_c(c')=\frac{|DEP(c)-DEP(c')|+|ARR(c)-ARR(c')|}{2}.$$
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
            //tex:
            // Perceived-journey-time advantage of base connection $$c$$:
            // $$y_c(c')=PJT(c')-PJT(c).$$
            return rhs.perceived_journey_time - lhs.perceived_journey_time;
        }

        double base_fare_quality_advantage(
              const SplitAlternative& lhs
            , const SplitAlternative& rhs
        ) noexcept {
            //tex:
            // Fare advantage of base connection $$c$$:
            // $$z_c(c')=FARE(c')-FARE(c).$$
            return rhs.metrics.fare - lhs.metrics.fare;
        }

        bool compared_connection_is_superior(
            double base_quality_advantage
        ) noexcept {
            return base_quality_advantage < 0.0;
        }

        double asymmetric_scale(
              double             base_quality_advantage
            , Dimless            higher_scale
            , Dimless            lower_scale
        ) noexcept {
            return compared_connection_is_superior(base_quality_advantage)
                ? mathfp::units::as_dimless(higher_scale)
                : mathfp::units::as_dimless(lower_scale);
        }

        double paper_independence_quality_scale(
              double                         base_quality_advantage
            , Dimless                        higher_scale
            , Dimless                        lower_scale
        ) noexcept {
            return asymmetric_scale(base_quality_advantage, higher_scale, lower_scale);
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
            //tex:
            // Non-negative influence $$f_c(c')$$ combines temporal proximity
            // and asymmetric quality/fare distances. Large similarity in
            // departure-arrival time and small quality/fare differences increase
            // overlap, thereby reducing $$IND(c)$$.
            // The production formula is the paper formula:
            // $$f_c(c')=\left(1-\frac{x_c(c')^+}{s_x}\right)^+\left(1-\gamma\min\left\{1,\frac{s_z|y_c(c')|+s_y|z_c(c')|}{s_y s_z}\right\}\right)^+.$$
            // The scales $$s_y$$ and $$s_z$$ are selected by the signs of
            // $$y_c(c')$$ and $$z_c(c')$$ respectively, as required by the
            // asymmetry rule in the article: a superior compared connection
            // $$c'$$ uses the higher-quality scale and therefore can exert
            // stronger influence on inferior base connection $$c$$.
            const auto x = temporal_similarity(base, other);
            const auto y = base_journey_quality_advantage(base, other);
            const auto z = base_fare_quality_advantage(base, other);
            const auto proximity = capped_proximity(
                  x
                , mathfp::units::as_dimless(config.temporal_similarity_scale)
            );
            const auto s_y = paper_independence_quality_scale(
                  y
                , config.higher_perceived_journey_time_scale
                , config.lower_perceived_journey_time_scale
            );
            const auto s_z = paper_independence_quality_scale(
                  z
                , config.higher_fare_scale
                , config.lower_fare_scale
            );
            const auto denominator = s_y * s_z;
            if (denominator <= 0.0) {
                return 0.0;
            }

            const auto quality_distance = std::min(
                  1.0
                , (
                      s_z * std::abs(y)
                    + s_y * std::abs(z)
                  ) / denominator
            );
            const auto quality_factor = 1.0
                - mathfp::units::as_dimless(config.gamma) * quality_distance;

            return proximity * std::max(0.0, quality_factor);
        }

        double split_independence(
              const SplitIndependenceConfig&    config
            , std::span<const SplitAlternative> alternatives
            , std::size_t                       index
        ) noexcept {
            //tex:
            // Independence of one connection within the OD set $$C$$:
            // $$IND(c)=\frac{1}{1+\sum_{c'\in C,\ c'\ne c}f_c(c')}.$$
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

        void assign_split_independences(
              std::vector<SplitAlternative>& alternatives
            , const SplitIndependenceConfig& config
        ) noexcept {
            for (std::size_t i = 0; i < alternatives.size(); ++i) {
                alternatives[i].independence = split_independence(
                      config
                    , alternatives
                    , i
                );
            }
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
                          .connection             = connection
                        , .day_path_alternative   = nullptr
                        , .source                 = DemandShareAlternativeSource::TimedConnection
                        , .day_path               = day_path_signature_of(*connection)
                        , .day_path_support       = nullptr
                        , .metrics                = metrics
                        , .perceived_journey_time = perceived_journey_time(
                              metrics
                            , params.perceived_journey_time
                          )
                        , .independence           = 0.0
                    }
                );
            }

            assign_split_independences(alternatives, params.independence);

            return alternatives;
        }

        std::vector<SplitAlternative> derive_split_alternatives(
              const std::vector<SplitConnectionAlternative>& supports
            , const SplitParams&                                    params
        ) {
            //tex:
            // Paper split semantics: each retained timed support is a separate
            // connection $$c\in C(a)$$. Independence is computed across timed
            // support alternatives, while DayPath remains only their structural
            // grouping identity.
            std::vector<SplitAlternative> alternatives;
            alternatives.reserve(supports.size());

            for (const auto& support : supports) {
                const auto& selected = *support.support;
                alternatives.push_back(
                    SplitAlternative{
                          .connection             = nullptr
                        , .day_path_alternative   = support.path
                        , .source                 = DemandShareAlternativeSource::DayPath
                        , .day_path               = day_path_signature_of(*support.path)
                        , .day_path_support       = &selected
                        , .metrics                = selected.connection_metrics
                        , .perceived_journey_time = perceived_journey_time(
                              selected.connection_metrics
                            , params.perceived_journey_time
                          )
                        , .independence           = 0.0
                    }
                );
            }

            assign_split_independences(alternatives, params.independence);

            return alternatives;
        }

        bool capacity_split_context_enabled(
              const SplitCapacityContext* context
            , const SplitParams&          params
        ) noexcept {
            return context != nullptr
                && context->config != nullptr
                && context->load_state != nullptr
                && context->capacity_set != nullptr
                && context->config->capacity_aware_split_enabled
                && mathfp::units::as_dimless(
                    params.perceived_journey_time.volume_capacity_ratio
                ) > 0.0;
        }

        mathfp::Expected<CapacityExposure> capacity_exposure_of(
              const SplitAlternative&      alternative
            , IntervalId                   interval
            , const SplitCapacityContext&  context
        ) {
            if (alternative.source == DemandShareAlternativeSource::DayPath) {
                if (alternative.day_path_support == nullptr) {
                    return mathfp::unexpected(
                        mathfp::internal_error("day-path split alternative is missing support envelope")
                    );
                }
                return day_path_support_capacity_exposure(
                      *alternative.day_path_support
                    , interval
                    , *context.load_state
                    , *context.capacity_set
                    , context.config->penalty_policy
                );
            }

            return connection_capacity_exposure(
                  connection_of(alternative)
                , interval
                , *context.load_state
                , *context.capacity_set
                , context.config->penalty_policy
            );
        }

        mathfp::Expected<std::vector<SplitAlternative>> apply_capacity_to_split_alternatives(
              const std::vector<SplitAlternative>& alternatives
            , IntervalId                           interval
            , const SplitParams&                   params
            , const SplitCapacityContext*          context
        ) {
            if (!capacity_split_context_enabled(context, params)) {
                return alternatives;
            }

            std::vector<SplitAlternative> adjusted;
            adjusted.reserve(alternatives.size());

            for (const auto& alternative : alternatives) {
                MATHFP_TRY_LET(
                          CapacityExposure
                    , exposure
                    , capacity_exposure_of(alternative, interval, *context)
                );
                MATHFP_TRY_LET(
                      double
                    , adjusted_perceived_journey_time
                    , capacity_adjusted_perceived_journey_time(
                          alternative.perceived_journey_time
                        , exposure
                        , params.perceived_journey_time.volume_capacity_ratio
                      )
                );

                adjusted.push_back(
                    SplitAlternative{
                          .connection             = alternative.connection
                        , .day_path_alternative   = alternative.day_path_alternative
                        , .source                 = alternative.source
                        , .day_path               = alternative.day_path
                        , .day_path_support       = alternative.day_path_support
                        , .metrics                = alternative.metrics
                        , .perceived_journey_time = adjusted_perceived_journey_time
                        , .independence           = 0.0
                    }
                );
            }

            assign_split_independences(adjusted, params.independence);
            return adjusted;
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

        using OdDayChoiceLookup = std::map<
              detail::grouping::OdKey
            , const OdDayPathChoicePairResult*
        >;

        mathfp::Expected<OdDayChoiceLookup> build_od_day_choice_lookup(
            const OdDayPathChoiceResult& choice_result
        ) {
            OdDayChoiceLookup lookup;
            for (const auto& origin_result : choice_result.origin_results) {
                for (const auto& pair_result : origin_result.pair_results) {
                    for (std::size_t i = 0; i < pair_result.alternatives.size(); ++i) {
                        MATHFP_TRY(validate_day_path_alternative(
                              pair_result.alternatives[i]
                            , i
                        ));
                    }
                    const auto key = detail::grouping::OdKey{
                          .origin      = pair_result.origin
                        , .destination = pair_result.destination
                    };
                    if (!lookup.emplace(key, &pair_result).second) {
                        return mathfp::unexpected(
                            mathfp::internal_error("OD-day choice result contains duplicate OD pair")
                                .ctx("origin"     , key.origin     .get())
                                .ctx("destination", key.destination.get())
                        );
                    }
                }
            }
            return lookup;
        }

        //tex:
        // Interval admissibility belongs here, not to OD-day search. For demand
        // interval $$a$$, this function constructs $$C(a)$$: all retained timed
        // connection supports whose demand reference time is admissible for
        // $$a$$. DayPath remains only the service-day structural identity.
        mathfp::Expected<IntervalSplitConnectionAlternatives>
        interval_split_connection_alternatives(
              const OdDayPathChoicePairResult&    pair_result
            , const TimeInterval&                 interval
            , const AssignmentPeriodConfig&       assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
        ) {
            IntervalSplitConnectionAlternatives result;
            result.alternatives.reserve(pair_result.alternatives.size());
            for (std::size_t i = 0; i < pair_result.alternatives.size(); ++i) {
                const auto& path = pair_result.alternatives[i];
                MATHFP_TRY(validate_day_path_alternative(path, i));
                for (const auto& support : day_path_split_support_descriptors(path)) {
                    ++result.candidate_supports;
                    const auto metrics = support.connection_metrics;
                    if (!connection_admissible_for_demand_segment(
                          metrics
                        , interval
                        , assignment_period
                        , admissibility_config
                    )) {
                        ++result.rejected_supports;
                        continue;
                    }
                    result.alternatives.push_back(
                        SplitConnectionAlternative{
                              .path    = &path
                            , .support = &support
                        }
                    );
                }
            }
            return result;
        }

        mathfp::Expected<mathfp::Unit> validate_paper_connection_split_supports(
              const DemandSplitResult&            split_result
            , const IntervalLookup&               interval_lookup
            , const AssignmentPeriodConfig&       assignment_period
            , const ConnectionAdmissibilityConfig& admissibility_config
            , DemandSegmentBasis                  demand_basis
        ) {
            if (admissibility_config.demand_time.basis != demand_basis) {
                return mathfp::unexpected(
                    mathfp::internal_error("OD-day split demand basis disagrees with interval admissibility basis")
                        .ctx("split_basis", std::string(to_string(demand_basis)))
                        .ctx(
                              "admissibility_basis"
                            , std::string(to_string(admissibility_config.demand_time.basis))
                        )
                );
            }

            for (std::size_t i = 0; i < split_result.shares.size(); ++i) {
                const auto& share = split_result.shares[i];
                if (share.source != DemandShareAlternativeSource::DayPath) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share is not a day-path interval support")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }
                if (share.day_path.origin != share.origin
                    || share.day_path.destination != share.destination) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share path identity disagrees with demand OD")
                            .ctx("share_index"     , static_cast<std::int64_t>(i))
                            .ctx("origin"          , share.origin.get())
                            .ctx("destination"     , share.destination.get())
                            .ctx("path_origin"     , share.day_path.origin.get())
                            .ctx("path_destination", share.day_path.destination.get())
                    );
                }
                if (!share.day_path_support.has_value()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share is missing compact interval support")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }
                if (!(share.day_path_support->signature == share.day_path)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share compact support disagrees with selected day path")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                    );
                }
                if (!(share.passengers > 0.0)
                    || !(share.probability > 0.0)
                    || !std::isfinite(share.passengers)
                    || !std::isfinite(share.probability)
                    || !std::isfinite(share.independence)
                    || !std::isfinite(share.split_impedance)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share has invalid numeric mass")
                            .ctx("share_index"    , static_cast<std::int64_t>(i))
                            .ctx("origin"         , share.origin.get())
                            .ctx("destination"    , share.destination.get())
                            .ctx("interval_id"    , share.interval.get())
                            .ctx("passengers"     , share.passengers)
                            .ctx("probability"    , share.probability)
                            .ctx("independence"   , share.independence)
                            .ctx("split_impedance", share.split_impedance)
                    );
                }

                const auto interval = find_interval(interval_lookup, share.interval);
                if (!interval) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share references unknown interval")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("interval_id", share.interval.get())
                    );
                }
                const auto metrics = share.day_path_support->connection_metrics;
                if (!connection_admissible_for_demand_segment(
                      metrics
                    , *interval
                    , assignment_period
                    , admissibility_config
                )) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split share was not selected from an interval-admissible support")
                            .ctx("share_index", static_cast<std::int64_t>(i))
                            .ctx("origin"     , share.origin.get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval.get())
                            .ctx("basis"      , std::string(to_string(demand_basis)))
                    );
                }
            }

            return mathfp::kUnit;
        }

        struct SplitDemandUnit final {
            ZoneId     origin;
            ZoneId     destination;
            IntervalId interval;
            double     passengers{};
        };

        void append_unassigned_demand(
              DemandSplitResult&      result
            , const SplitDemandUnit&  demand
            , UnassignedDemandReason  reason
        ) {
            result.unassigned.push_back(
                UnassignedDemand{
                      .origin      = demand.origin
                    , .destination = demand.destination
                    , .interval    = demand.interval
                    , .passengers  = demand.passengers
                    , .reason      = reason
                }
            );
        }

        void append_od_day_paper_split_certificate(
              DemandSplitResult&                 result
            , const SplitDemandUnit&             demand
            , std::size_t                        candidate_support_count
            , std::size_t                        interval_admissible_support_count
            , std::size_t                        interval_rejected_support_count
            , std::size_t                        share_begin
            , std::optional<UnassignedDemandReason> unassigned_reason
        ) {
            mathfp::CompensatedSum<double> assigned;
            mathfp::CompensatedSum<double> probability;
            for (std::size_t i = share_begin; i < result.shares.size(); ++i) {
                assigned.add(result.shares[i].passengers);
                probability.add(result.shares[i].probability);
            }
            const auto assigned_passengers = assigned.value();
            result.od_day_paper_split.push_back(
                OdDayPaperSplitCertificate{
                      .origin                            = demand.origin
                    , .destination                       = demand.destination
                    , .interval                          = demand.interval
                    , .candidate_support_count           = candidate_support_count
                    , .interval_admissible_support_count = interval_admissible_support_count
                    , .interval_rejected_support_count   = interval_rejected_support_count
                    , .share_count                       = result.shares.size() - share_begin
                    , .demand_passengers                 = demand.passengers
                    , .assigned_passengers               = assigned_passengers
                    , .unassigned_passengers             =
                          unassigned_reason.has_value() ? demand.passengers : 0.0
                    , .probability_sum                   = probability.value()
                    , .unassigned_reason                 = unassigned_reason
                }
            );
        }

        struct OdDayDemandConservationSummary final {
            std::size_t demand_intervals{};
            std::size_t assigned_intervals{};
            std::size_t unassigned_intervals{};
            double      demand_passengers{};
            double      assigned_passengers{};
            double      unassigned_passengers{};
        };

        [[nodiscard]] bool same_demand_mass(
              double actual
            , double expected
        ) noexcept {
            return mathfp::almost_equal(
                  actual
                , expected
                , mathfp::abs_tolerance(expected)
                , mathfp::rel_tolerance_coeff<double>()
            );
        }

        [[nodiscard]] mathfp::Expected<OdDayDemandConservationSummary>
        validate_od_day_demand_conservation(
              const DemandSplitResult&    split_result
            , const InputModel&           input
            , std::optional<ZoneId>       origin_filter = std::nullopt
        ) {
            std::map<detail::grouping::DemandKey, const DemandEntry*> demand_by_key;
            mathfp::CompensatedSum<double> total_demand;
            for (const auto& demand : input.demand) {
                if (demand.passengers <= 0.0) {
                    continue;
                }
                if (origin_filter.has_value() && demand.origin != *origin_filter) {
                    continue;
                }
                const auto key = detail::grouping::demand_key(demand);
                if (!demand_by_key.emplace(key, &demand).second) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day demand conservation received duplicate demand key")
                            .ctx("origin"     , demand.origin     .get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval   .get())
                    );
                }
                total_demand.add(demand.passengers);
            }

            std::map<detail::grouping::DemandKey, mathfp::CompensatedSum<double>>
                passenger_sum_by_key;
            std::map<detail::grouping::DemandKey, mathfp::CompensatedSum<double>>
                probability_sum_by_key;
            std::map<detail::grouping::DemandKey, mathfp::CompensatedSum<double>>
                unassigned_sum_by_key;
            mathfp::CompensatedSum<double> total_assigned;
            mathfp::CompensatedSum<double> total_unassigned;

            for (const auto& share : split_result.shares) {
                if (origin_filter.has_value() && share.origin != *origin_filter) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day origin split contains share outside requested origin")
                            .ctx("origin"      , origin_filter->get())
                            .ctx("share_origin", share.origin.get())
                    );
                }
                const auto key = detail::grouping::demand_key(share);
                if (!demand_by_key.contains(key)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split conservation found share without positive demand")
                            .ctx("origin"     , share.origin     .get())
                            .ctx("destination", share.destination.get())
                            .ctx("interval_id", share.interval   .get())
                    );
                }
                passenger_sum_by_key[key].add(share.passengers);
                probability_sum_by_key[key].add(share.probability);
                total_assigned.add(share.passengers);
            }

            for (const auto& unassigned : split_result.unassigned) {
                if (origin_filter.has_value() && unassigned.origin != *origin_filter) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day origin split contains unassigned demand outside requested origin")
                            .ctx("origin"             , origin_filter->get())
                            .ctx("unassigned_origin"  , unassigned.origin.get())
                            .ctx("unassigned_reason"  , std::string(to_string(unassigned.reason)))
                    );
                }
                if (!(unassigned.passengers > 0.0) || !std::isfinite(unassigned.passengers)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day unassigned demand has invalid passenger mass")
                            .ctx("origin"     , unassigned.origin     .get())
                            .ctx("destination", unassigned.destination.get())
                            .ctx("interval_id", unassigned.interval   .get())
                            .ctx("passengers" , unassigned.passengers)
                            .ctx("reason"     , std::string(to_string(unassigned.reason)))
                    );
                }
                const auto key = detail::grouping::demand_key(unassigned);
                if (!demand_by_key.contains(key)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split conservation found unassigned mass without positive demand")
                            .ctx("origin"     , unassigned.origin     .get())
                            .ctx("destination", unassigned.destination.get())
                            .ctx("interval_id", unassigned.interval   .get())
                            .ctx("reason"     , std::string(to_string(unassigned.reason)))
                    );
                }
                unassigned_sum_by_key[key].add(unassigned.passengers);
                total_unassigned.add(unassigned.passengers);
            }

            for (const auto& [key, demand] : demand_by_key) {
                const auto passenger_sum =
                    passenger_sum_by_key.contains(key)
                        ? passenger_sum_by_key[key].value()
                        : 0.0;
                const auto unassigned_sum =
                    unassigned_sum_by_key.contains(key)
                        ? unassigned_sum_by_key[key].value()
                        : 0.0;

                if (passenger_sum > 0.0) {
                    const auto probability_sum = probability_sum_by_key[key].value();
                    if (!same_demand_mass(probability_sum, 1.0)) {
                        return mathfp::unexpected(
                            mathfp::internal_error("OD-day split probabilities do not conserve assigned unit mass")
                                .ctx("origin"         , key.origin     .get())
                                .ctx("destination"    , key.destination.get())
                                .ctx("interval_id"    , key.interval   .get())
                                .ctx("probability_sum", probability_sum)
                        );
                    }
                }

                const auto conserved_sum = passenger_sum + unassigned_sum;
                if (!same_demand_mass(conserved_sum, demand->passengers)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day split does not conserve demand as assigned plus explicit unassigned mass")
                            .ctx("origin"           , key.origin     .get())
                            .ctx("destination"      , key.destination.get())
                            .ctx("interval_id"      , key.interval   .get())
                            .ctx("assigned_sum"     , passenger_sum)
                            .ctx("unassigned_sum"   , unassigned_sum)
                            .ctx("conserved_sum"    , conserved_sum)
                            .ctx("demand_passengers", demand->passengers)
                    );
                }
            }

            return OdDayDemandConservationSummary{
                  .demand_intervals     = demand_by_key.size()
                , .assigned_intervals   = passenger_sum_by_key.size()
                , .unassigned_intervals = unassigned_sum_by_key.size()
                , .demand_passengers    = total_demand.value()
                , .assigned_passengers  = total_assigned.value()
                , .unassigned_passengers = total_unassigned.value()
            };
        }

        mathfp::Expected<mathfp::Unit> validate_od_day_paper_split_certificates(
              const DemandSplitResult& split_result
            , const InputModel&        input
            , std::optional<ZoneId>    origin_filter = std::nullopt
        ) {
            std::map<detail::grouping::DemandKey, const OdDayPaperSplitCertificate*> certificates;
            for (const auto& certificate : split_result.od_day_paper_split) {
                if (origin_filter.has_value() && certificate.origin != *origin_filter) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day paper split certificate outside requested origin")
                            .ctx("origin", origin_filter->get())
                            .ctx("certificate_origin", certificate.origin.get())
                    );
                }
                const auto key = detail::grouping::DemandKey{
                      .origin      = certificate.origin
                    , .destination = certificate.destination
                    , .interval    = certificate.interval
                };
                if (!certificates.emplace(key, &certificate).second) {
                    return mathfp::unexpected(
                        mathfp::internal_error("duplicate OD-day paper split certificate")
                            .ctx("origin"     , key.origin.get())
                            .ctx("destination", key.destination.get())
                            .ctx("interval_id", key.interval.get())
                    );
                }
            }

            for (const auto& demand : input.demand) {
                if (demand.passengers <= 0.0) {
                    continue;
                }
                if (origin_filter.has_value() && demand.origin != *origin_filter) {
                    continue;
                }
                const auto key = detail::grouping::demand_key(demand);
                const auto it = certificates.find(key);
                if (it == certificates.end()) {
                    return mathfp::unexpected(
                        mathfp::internal_error("missing OD-day paper split certificate")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                    );
                }

                const auto& certificate = *it->second;
                if (!same_demand_mass(certificate.demand_passengers, demand.passengers)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day paper split certificate demand mass mismatch")
                            .ctx("origin"                , demand.origin.get())
                            .ctx("destination"           , demand.destination.get())
                            .ctx("interval_id"           , demand.interval.get())
                            .ctx("certificate_passengers", certificate.demand_passengers)
                            .ctx("demand_passengers"     , demand.passengers)
                    );
                }

                if (certificate.interval_admissible_support_count > 0u) {
                    if (certificate.share_count == 0u
                        || certificate.unassigned_reason.has_value()
                        || !same_demand_mass(certificate.probability_sum, 1.0)
                        || !same_demand_mass(certificate.assigned_passengers, demand.passengers)
                        || !same_demand_mass(certificate.unassigned_passengers, 0.0)) {
                        return mathfp::unexpected(
                            mathfp::internal_error("OD-day paper split certificate violates nonempty C(a) assignment")
                                .ctx("origin"     , demand.origin.get())
                                .ctx("destination", demand.destination.get())
                                .ctx("interval_id", demand.interval.get())
                                .ctx(
                                      "c_a_size"
                                    , static_cast<std::int64_t>(
                                          certificate.interval_admissible_support_count
                                      )
                                  )
                                .ctx("share_count", static_cast<std::int64_t>(certificate.share_count))
                                .ctx("probability_sum", certificate.probability_sum)
                                .ctx("assigned", certificate.assigned_passengers)
                                .ctx("unassigned", certificate.unassigned_passengers)
                        );
                    }
                    continue;
                }

                if (certificate.share_count != 0u
                    || !certificate.unassigned_reason.has_value()
                    || !same_demand_mass(certificate.probability_sum, 0.0)
                    || !same_demand_mass(certificate.assigned_passengers, 0.0)
                    || !same_demand_mass(certificate.unassigned_passengers, demand.passengers)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("OD-day paper split certificate violates empty C(a) unassignment")
                            .ctx("origin"     , demand.origin.get())
                            .ctx("destination", demand.destination.get())
                            .ctx("interval_id", demand.interval.get())
                            .ctx("share_count", static_cast<std::int64_t>(certificate.share_count))
                            .ctx("probability_sum", certificate.probability_sum)
                            .ctx("assigned", certificate.assigned_passengers)
                            .ctx("unassigned", certificate.unassigned_passengers)
                    );
                }
            }

            return mathfp::kUnit;
        }

        mathfp::Expected<std::size_t> append_split_shares(
              DemandSplitResult&                    result
            , const SplitDemandUnit&                demand
            , const TimeInterval&                   interval
            , const std::vector<SplitAlternative>&  base_alternatives
            , const SearchParams&                   params
            , DemandSegmentBasis                    demand_basis
            , const SplitCapacityContext*           capacity_context
        ) {
            //tex:
            // Final assignment for one interval $$a$$ normalizes choice weights:
            // $$P_a(c)=\frac{w_a(c)}{\sum_{\tilde c\in C(a)}w_a(\tilde c)}DEM(a),\qquad w_a(c)=\exp(-\beta b^{(t)}(IMP_a(c)))IND(c).$$
            // Computation is performed in log space and the residual alternative
            // receives rounding mass so probabilities and passenger totals conserve
            // $$DEM(a)$$.
            if (base_alternatives.empty()) {
                return std::size_t{ 0 };
            }

            MATHFP_TRY_LET(
                  std::vector<SplitAlternative>
                , alternatives
                , apply_capacity_to_split_alternatives(
                      base_alternatives
                    , demand.interval
                    , params.split
                    , capacity_context
                )
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
                    , interval
                    , params.split
                    , demand_basis
                );
                const auto choice_impedance = transform_split_impedance(
                      params.split.impedance_transform
                    , imp
                );
                MATHFP_TRY_LET(double, log_weight, split_choice_log_weight(
                      params.split.choice_model
                    , choice_impedance
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
                weights.push_back(std::exp(log_weight - max_log_weight));
            }
            const auto weight_sum = mathfp::compensated_sum(weights);
            if (!(weight_sum > 0.0) || !std::isfinite(weight_sum)) {
                return mathfp::unexpected(
                    mathfp::domain_error("invalid OD-day split weight normalization")
                        .ctx("origin"     , demand.origin.get())
                        .ctx("destination", demand.destination.get())
                        .ctx("interval"   , demand.interval.get())
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
                    mathfp::domain_error("invalid OD-day residual split normalization")
                        .ctx("origin"              , demand.origin.get())
                        .ctx("destination"         , demand.destination.get())
                        .ctx("interval"            , demand.interval.get())
                        .ctx("residual_probability", residual_probability)
                        .ctx("residual_passengers" , residual_passengers)
                );
            }
            residual_probability = std::max(0.0, residual_probability);
            residual_passengers = std::max(0.0, residual_passengers);
            probabilities[residual_index] = residual_probability;
            passengers[residual_index] = residual_passengers;

            const auto suppressed = compact_numerical_support(
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
                        , .source          = alternatives[i].source
                        , .day_path        = alternatives[i].day_path
                        , .connection      = connection_of(alternatives[i])
                        , .day_path_support = materialize_day_path_support(alternatives[i])
                        , .passengers      = passengers[i]
                        , .probability     = probabilities[i]
                        , .independence    = independences[i]
                        , .split_impedance = split_impedances[i]
                    }
                );
            }

            return suppressed;
        }

        mathfp::Expected<DemandSplitResult> split_demand_over_connections_impl(
              const ConnectionChoiceResult& choice_result
            , const InputModel&             input
            , const SearchParams&           params
            , const DemandSegmentTimeConfig& demand_segment_time
            , const SplitCapacityContext*   capacity_context
            , std::string_view              progress_label
            , std::string_view              result_label
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        MATHFP_TRY(validate_supported_split_choice_model(params.split.choice_model.model));
        MATHFP_TRY(validate_demand_segment_time_config(demand_segment_time));

        both(progress_label);
        log(
            fmt::format(
                  "split input: chosen_connections = {:>8}  choice_tasks = {:>8}  demand_entries = {:>8}  choice_model = {}  demand_basis = {}  capacity_aware = {}"
                , choice_result.connections.size()
                , choice_result.task_results.size()
                , input.demand.size()
                , to_string(params.split.choice_model.model)
                , to_string(demand_segment_time.basis)
                , capacity_split_context_enabled(capacity_context, params.split) ? "true" : "false"
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
            const auto base_alternatives = derive_split_alternatives(
                  task_connections
                , params.split
            );
            MATHFP_TRY_LET(
                  std::vector<SplitAlternative>
                , alternatives
                , apply_capacity_to_split_alternatives(
                      base_alternatives
                    , demand.interval
                    , params.split
                    , capacity_context
                )
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
                const auto choice_impedance = transform_split_impedance(
                      params.split.impedance_transform
                    , imp
                );
                MATHFP_TRY_LET(double, log_weight, split_choice_log_weight(
                      params.split.choice_model
                    , choice_impedance
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
                        , .source          = alternatives[i].source
                        , .day_path        = alternatives[i].day_path
                        , .connection      = connection_of(alternatives[i])
                        , .day_path_support = materialize_day_path_support(alternatives[i])
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
        both(result_label);
        return result;
    }

    }  // namespace

    mathfp::Expected<std::vector<OdDemandIntervals>> build_od_demand_intervals(
        const InputModel& input
    ) {
        std::map<detail::grouping::OdKey, std::vector<OdDemandInterval>> grouped;
        for (const auto& demand : input.demand) {
            if (demand.passengers <= 0.0) {
                continue;
            }
            grouped[detail::grouping::od_key(demand)].push_back(
                OdDemandInterval{
                      .origin      = demand.origin
                    , .destination = demand.destination
                    , .interval    = demand.interval
                    , .passengers  = demand.passengers
                }
            );
        }

        std::vector<OdDemandIntervals> result;
        result.reserve(grouped.size());
        for (auto& [key, intervals] : grouped) {
            result.push_back(
                OdDemandIntervals{
                      .origin      = key.origin
                    , .destination = key.destination
                    , .intervals   = std::move(intervals)
                }
            );
        }
        return result;
    }

    mathfp::Expected<DemandSplitResult> split_demand_over_connections(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
        , const DemandSegmentTimeConfig& demand_segment_time
    ) {
        return split_demand_over_connections_impl(
              choice_result
            , input
            , params
            , demand_segment_time
            , nullptr
            , "split: demand assignment"
            , "split: demand assignment done"
        );
    }

    mathfp::Expected<DemandSplitResult> split_demand_over_od_day_paths(
          const OdDayPathChoiceResult&       choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        MATHFP_TRY(validate_supported_split_choice_model(params.split.choice_model.model));
        MATHFP_TRY(validate_demand_segment_time_config(demand_segment_time));
        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));

        both("split: OD-day demand assignment");
        log(
            fmt::format(
                  "OD-day split input: chosen_connections = {:>8}  origin_results = {:>8}  demand_entries = {:>8}  choice_model = {}  demand_basis = {}"
                , choice_result.connections.size()
                , choice_result.origin_results.size()
                , input.demand.size()
                , to_string(params.split.choice_model.model)
                , to_string(demand_segment_time.basis)
            )
            , LogLevel::Info
        );

        MATHFP_TRY_LET(
              OdDayChoiceLookup
            , choice_lookup
            , build_od_day_choice_lookup(choice_result)
        );
        MATHFP_TRY_LET(
              std::vector<OdDemandIntervals>
            , demand_intervals
            , build_od_demand_intervals(input)
        );
        const auto interval_lookup = build_interval_lookup(input);

        DemandSplitResult result;
        std::size_t suppressed_numerical_shares = 0;
        std::size_t interval_count = 0;
        std::size_t skipped_empty_alternatives = 0;
        std::size_t skipped_temporally_inadmissible = 0;

        for (const auto& od_demand : demand_intervals) {
            const auto od_key = detail::grouping::OdKey{
                  .origin      = od_demand.origin
                , .destination = od_demand.destination
            };
            const auto choice_it = choice_lookup.find(od_key);

            for (const auto& demand : od_demand.intervals) {
                ++interval_count;
                const auto interval = find_interval(interval_lookup, demand.interval);
                if (!interval) {
                    return mathfp::unexpected(
                        mathfp::invalid_arg("OD demand interval references unknown time interval")
                            .ctx("interval_id", demand.interval.get())
                    );
                }
                const auto demand_unit = SplitDemandUnit{
                      .origin      = demand.origin
                    , .destination = demand.destination
                    , .interval    = demand.interval
                    , .passengers  = demand.passengers
                };
                if (choice_it == choice_lookup.end()) {
                    ++skipped_empty_alternatives;
                    const auto share_begin = result.shares.size();
                    append_unassigned_demand(
                          result
                        , demand_unit
                        , UnassignedDemandReason::NoChosenAlternatives
                    );
                    append_od_day_paper_split_certificate(
                          result
                        , demand_unit
                        , 0u
                        , 0u
                        , 0u
                        , share_begin
                        , UnassignedDemandReason::NoChosenAlternatives
                    );
                    continue;
                }

                MATHFP_TRY_LET(
                      IntervalSplitConnectionAlternatives
                    , split_connections
                    , interval_split_connection_alternatives(
                      *choice_it->second
                    , *interval
                    , assignment_period
                    , admissibility_config
                    )
                );
                skipped_temporally_inadmissible += split_connections.rejected_supports;
                if (split_connections.alternatives.empty()) {
                    ++skipped_empty_alternatives;
                    const auto share_begin = result.shares.size();
                    append_unassigned_demand(
                          result
                        , demand_unit
                        , UnassignedDemandReason::NoIntervalAdmissibleSupport
                    );
                    append_od_day_paper_split_certificate(
                          result
                        , demand_unit
                        , split_connections.candidate_supports
                        , split_connections.alternatives.size()
                        , split_connections.rejected_supports
                        , share_begin
                        , UnassignedDemandReason::NoIntervalAdmissibleSupport
                    );
                    continue;
                }
                const auto base_alternatives = derive_split_alternatives(
                      split_connections.alternatives
                    , params.split
                );

                const auto share_begin = result.shares.size();
                MATHFP_TRY_LET(
                      std::size_t
                    , suppressed
                    , append_split_shares(
                          result
                        , SplitDemandUnit{
                              .origin      = demand.origin
                            , .destination = demand.destination
                            , .interval    = demand.interval
                            , .passengers  = demand.passengers
                          }
                        , *interval
                        , base_alternatives
                        , params
                        , demand_segment_time.basis
                        , nullptr
                    )
                );
                suppressed_numerical_shares += suppressed;
                append_od_day_paper_split_certificate(
                      result
                    , demand_unit
                    , split_connections.candidate_supports
                    , split_connections.alternatives.size()
                    , split_connections.rejected_supports
                    , share_begin
                    , std::nullopt
                );
            }
        }

        MATHFP_TRY(validate_paper_connection_split_supports(
              result
            , interval_lookup
            , assignment_period
            , admissibility_config
            , demand_segment_time.basis
        ));
        MATHFP_TRY_LET(
              OdDayDemandConservationSummary
            , conservation
            , validate_od_day_demand_conservation(
                  result
                , input
            )
        );
        MATHFP_TRY(validate_od_day_paper_split_certificates(result, input));
        log(
            fmt::format(
                  "OD-day split result: split_contract=paper_connection_split support_selection=all_interval_admissible day_path_identity=post_layer single_best_support=disabled conservation=assigned_plus_unassigned demand_od = {:>8}  demand_intervals = {:>8}  assigned_intervals = {:>8}  unassigned_intervals = {:>8}  shares = {:>8}  unassigned = {:>8}  demand_passengers = {:.6f}  assigned_passengers = {:.6f}  unassigned_passengers = {:.6f}  skipped_empty_intervals = {:>8}  inadmissible_supports = {:>8}  suppressed_numerical_shares = {:>8}"
                , demand_intervals.size()
                , interval_count
                , conservation.assigned_intervals
                , conservation.unassigned_intervals
                , result.shares.size()
                , result.unassigned.size()
                , conservation.demand_passengers
                , conservation.assigned_passengers
                , conservation.unassigned_passengers
                , skipped_empty_alternatives
                , skipped_temporally_inadmissible
                , suppressed_numerical_shares
            )
            , LogLevel::Info
        );
        both("split: OD-day demand assignment done");
        return result;
    }

    mathfp::Expected<DemandSplitResult> split_origin_demand_over_od_day_paths(
          const OriginDayPathChoiceResult&   choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        MATHFP_TRY(validate_supported_split_choice_model(params.split.choice_model.model));
        MATHFP_TRY(validate_demand_segment_time_config(demand_segment_time));
        MATHFP_TRY(validate_assignment_period_config(assignment_period));
        MATHFP_TRY(validate_connection_admissibility_config(admissibility_config));

        std::map<detail::grouping::OdKey, const OdDayPathChoicePairResult*> choice_lookup;
        for (const auto& pair_result : choice_result.pair_results) {
            const auto key = detail::grouping::OdKey{
                  .origin      = pair_result.origin
                , .destination = pair_result.destination
            };
            if (key.origin != choice_result.origin) {
                return mathfp::unexpected(
                    mathfp::internal_error("origin-day choice result contains pair with another origin")
                        .ctx("origin", choice_result.origin.get())
                        .ctx("pair_origin", key.origin.get())
                );
            }
            if (!choice_lookup.emplace(key, &pair_result).second) {
                return mathfp::unexpected(
                    mathfp::internal_error("origin-day choice result contains duplicate OD pair")
                        .ctx("origin"     , key.origin     .get())
                        .ctx("destination", key.destination.get())
                );
            }
        }

        const auto interval_lookup = build_interval_lookup(input);
        DemandSplitResult result;

        for (const auto& demand : input.demand) {
            if (demand.passengers <= 0.0 || demand.origin != choice_result.origin) {
                continue;
            }
            const auto demand_unit = SplitDemandUnit{
                  .origin      = demand.origin
                , .destination = demand.destination
                , .interval    = demand.interval
                , .passengers  = demand.passengers
            };
            const auto od_key = detail::grouping::od_key(demand);
            const auto choice_it = choice_lookup.find(od_key);
            if (choice_it == choice_lookup.end()) {
                const auto share_begin = result.shares.size();
                append_unassigned_demand(
                      result
                    , demand_unit
                    , UnassignedDemandReason::NoChosenAlternatives
                );
                append_od_day_paper_split_certificate(
                      result
                    , demand_unit
                    , 0u
                    , 0u
                    , 0u
                    , share_begin
                    , UnassignedDemandReason::NoChosenAlternatives
                );
                continue;
            }
            const auto interval = find_interval(interval_lookup, demand.interval);
            if (!interval) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("origin OD demand interval references unknown time interval")
                        .ctx("interval_id", demand.interval.get())
                );
            }

            MATHFP_TRY_LET(
                  IntervalSplitConnectionAlternatives
                , split_connections
                , interval_split_connection_alternatives(
                  *choice_it->second
                , *interval
                , assignment_period
                , admissibility_config
                )
            );
            if (split_connections.alternatives.empty()) {
                const auto share_begin = result.shares.size();
                append_unassigned_demand(
                      result
                    , demand_unit
                    , UnassignedDemandReason::NoIntervalAdmissibleSupport
                );
                append_od_day_paper_split_certificate(
                      result
                    , demand_unit
                    , split_connections.candidate_supports
                    , split_connections.alternatives.size()
                    , split_connections.rejected_supports
                    , share_begin
                    , UnassignedDemandReason::NoIntervalAdmissibleSupport
                );
                continue;
            }
            const auto base_alternatives = derive_split_alternatives(
                  split_connections.alternatives
                , params.split
            );
            const auto share_begin = result.shares.size();
            MATHFP_TRY_LET(
                  std::size_t
                , suppressed
                , append_split_shares(
                      result
                    , SplitDemandUnit{
                          .origin      = demand.origin
                        , .destination = demand.destination
                        , .interval    = demand.interval
                        , .passengers  = demand.passengers
                      }
                    , *interval
                    , base_alternatives
                    , params
                    , demand_segment_time.basis
                    , nullptr
                )
            );
            (void)suppressed;
            append_od_day_paper_split_certificate(
                  result
                , demand_unit
                , split_connections.candidate_supports
                , split_connections.alternatives.size()
                , split_connections.rejected_supports
                , share_begin
                , std::nullopt
            );
        }

        MATHFP_TRY(validate_paper_connection_split_supports(
              result
            , interval_lookup
            , assignment_period
            , admissibility_config
            , demand_segment_time.basis
        ));
        MATHFP_TRY(validate_od_day_demand_conservation(
              result
            , input
            , std::optional<ZoneId>{ choice_result.origin }
        ));
        MATHFP_TRY(validate_od_day_paper_split_certificates(
              result
            , input
            , std::optional<ZoneId>{ choice_result.origin }
        ));
        return result;
    }

    mathfp::Expected<OriginDayDemandLoadResult> load_origin_day_path_demand(
          const OriginDaySearchResult&       search_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::log;

        MATHFP_TRY_LET(
              OriginDayPathChoiceResult
            , alternatives
            , choose_origin_day_paths(
                  search_result
                , params
                , search_cost
                , choice_config
            )
        );
        MATHFP_TRY_LET(
              DemandSplitResult
            , split_result
            , split_origin_demand_over_od_day_paths(
                  alternatives
                , input
                , params
                , demand_segment_time
                , assignment_period
                , admissibility_config
            )
        );
        MATHFP_TRY_LET(
              ElementarySegmentLoads
            , elementary_segment_loads
            , build_day_path_elementary_segment_loads(split_result)
        );
        MATHFP_TRY_LET(
              OdDayDemandConservationSummary
            , conservation
            , validate_od_day_demand_conservation(
                  split_result
                , input
                , std::optional<ZoneId>{ search_result.origin }
            )
        );
        log(
            fmt::format(
                  "OD-day origin load: origin={} split_contract=paper_connection_split support_selection=all_interval_admissible day_path_identity=post_layer single_best_support=disabled conservation=assigned_plus_unassigned demand_intervals={} assigned_intervals={} unassigned_intervals={} shares={} unassigned={} demand_passengers={:.6f} assigned_passengers={:.6f} unassigned_passengers={:.6f} elementary_loads={}"
                , search_result.origin.get()
                , conservation.demand_intervals
                , conservation.assigned_intervals
                , conservation.unassigned_intervals
                , split_result.shares.size()
                , split_result.unassigned.size()
                , conservation.demand_passengers
                , conservation.assigned_passengers
                , conservation.unassigned_passengers
                , elementary_segment_loads.items.size()
            )
            , LogLevel::Info
        );
        return OriginDayDemandLoadResult{
              .alternatives              = std::move(alternatives)
            , .split_result              = std::move(split_result)
            , .elementary_segment_loads  = std::move(elementary_segment_loads)
        };
    }

    mathfp::Expected<DemandSplitResult> split_demand_over_od_day_connections(
          const OdDayConnectionChoiceResult& choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        return split_demand_over_od_day_paths(
              choice_result
            , input
            , params
            , demand_segment_time
            , assignment_period
            , admissibility_config
        );
    }

    mathfp::Expected<DemandSplitResult> split_origin_demand_over_od_day_connections(
          const OriginDayChoiceResult&       choice_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        return split_origin_demand_over_od_day_paths(
              choice_result
            , input
            , params
            , demand_segment_time
            , assignment_period
            , admissibility_config
        );
    }

    mathfp::Expected<OriginDayDemandLoadResult> load_origin_day_demand(
          const OriginDaySearchResult&       search_result
        , const InputModel&                  input
        , const SearchParams&                params
        , const SearchCostContext&           search_cost
        , const ChoiceConfig&                choice_config
        , const DemandSegmentTimeConfig&     demand_segment_time
        , const AssignmentPeriodConfig&      assignment_period
        , const ConnectionAdmissibilityConfig& admissibility_config
    ) {
        return load_origin_day_path_demand(
              search_result
            , input
            , params
            , search_cost
            , choice_config
            , demand_segment_time
            , assignment_period
            , admissibility_config
        );
    }

    mathfp::Expected<DemandSplitResult> split_demand_over_connections_capacity_aware(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
        , const DemandSegmentTimeConfig& demand_segment_time
        , const CapacityAwareAssignmentConfig& capacity_config
        , const VehicleJourneyItemLoadState& load_state
        , const VehicleJourneyItemCapacitySet& capacity_set
    ) {
        MATHFP_TRY(validate_capacity_aware_assignment_config(capacity_config));
        MATHFP_TRY(validate_vehicle_journey_item_load_state(load_state));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacity_set));

        const auto capacity_context = SplitCapacityContext{
              .config       = &capacity_config
            , .load_state   = &load_state
            , .capacity_set = &capacity_set
        };

        return split_demand_over_connections_impl(
              choice_result
            , input
            , params
            , demand_segment_time
            , &capacity_context
            , "split: capacity-aware demand assignment"
            , "split: capacity-aware demand assignment done"
        );
    }

    mathfp::Expected<CapacityAwareDemandSplitResult> iterate_capacity_aware_split(
          const ConnectionChoiceResult& choice_result
        , const InputModel&             input
        , const SearchParams&           params
        , const DemandSegmentTimeConfig& demand_segment_time
        , const CapacityAwareAssignmentConfig& capacity_config
        , const VehicleJourneyItemCapacitySet& capacity_set
    ) {
        using timetable::infra::LogLevel;
        using timetable::infra::progress::both;
        using timetable::infra::progress::log;

        MATHFP_TRY(validate_capacity_aware_assignment_config(capacity_config));
        MATHFP_TRY(validate_vehicle_journey_item_capacity_set(capacity_set));

        const auto capacity_factor = mathfp::units::as_dimless(
            params.split.perceived_journey_time.volume_capacity_ratio
        );
        const auto enabled = capacity_config.capacity_aware_split_enabled
            && capacity_factor > 0.0;

        if (!enabled) {
            MATHFP_TRY_LET(
                  DemandSplitResult
                , split_result
                , split_demand_over_connections(
                      choice_result
                    , input
                    , params
                    , demand_segment_time
                )
            );
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoads
                , split_loads
                , build_vehicle_journey_item_loads(split_result)
            );
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoadState
                , load_state
                , make_vehicle_journey_item_load_state(split_loads)
            );

            return CapacityAwareDemandSplitResult{
                  .split_result = std::move(split_result)
                , .split_loads  = std::move(split_loads)
                , .load_state   = std::move(load_state)
                , .diagnostics  = make_capacity_aware_split_disabled_diagnostics()
            };
        }

        both("split: capacity-aware fixed-point iteration");
        log(
            fmt::format(
                  "capacity-aware split input: max_iterations = {}  abs_tol = {}  rel_tol = {}  penalty_policy = {}"
                , capacity_config.iteration.max_iterations
                , capacity_config.iteration.absolute_load_tolerance
                , capacity_config.iteration.relative_load_tolerance
                , to_string(capacity_config.penalty_policy)
            )
            , LogLevel::Info
        );

        VehicleJourneyItemLoadState current_state{};
        DemandSplitResult           last_split_result{};
        VehicleJourneyItemLoads     last_split_loads{};
        CapacityAwareSplitDiagnostics diagnostics{
              .enabled                 = true
            , .iterations              = 0
            , .converged               = false
            , .max_load_delta          = 0.0
            , .max_relative_load_delta = 0.0
        };

        for (std::int32_t iteration = 1;
             iteration <= capacity_config.iteration.max_iterations;
             ++iteration) {
            MATHFP_TRY_LET(
                  DemandSplitResult
                , candidate_split
                , split_demand_over_connections_capacity_aware(
                      choice_result
                    , input
                    , params
                    , demand_segment_time
                    , capacity_config
                    , current_state
                    , capacity_set
                )
            );
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoads
                , candidate_loads
                , build_vehicle_journey_item_loads(candidate_split)
            );

            const auto alpha = 1.0 / static_cast<double>(iteration);
            MATHFP_TRY_LET(
                  VehicleJourneyItemLoadState
                , next_state
                , msa_update_load_state(
                      current_state
                    , candidate_loads
                    , alpha
                )
            );

            const auto delta = load_state_delta(current_state, next_state);
            diagnostics.iterations              = iteration;
            diagnostics.max_load_delta          = delta.max_absolute;
            diagnostics.max_relative_load_delta = delta.max_relative;

            last_split_result = std::move(candidate_split);
            last_split_loads  = std::move(candidate_loads);
            current_state     = std::move(next_state);

            if (capacity_iteration_converged(capacity_config.iteration, delta)) {
                diagnostics.converged = true;
                break;
            }
        }

        MATHFP_TRY(validate_capacity_aware_split_diagnostics(diagnostics));
        MATHFP_TRY(validate_vehicle_journey_item_load_state(current_state));
        MATHFP_TRY(validate_vehicle_journey_item_loads(last_split_loads));

        log(
            fmt::format(
                  "capacity-aware split result: iterations = {}  converged = {}  max_load_delta = {}  max_relative_load_delta = {}"
                , diagnostics.iterations
                , diagnostics.converged ? "true" : "false"
                , diagnostics.max_load_delta
                , diagnostics.max_relative_load_delta
            )
            , LogLevel::Info
        );
        both("split: capacity-aware fixed-point iteration done");

        return CapacityAwareDemandSplitResult{
              .split_result = std::move(last_split_result)
            , .split_loads  = std::move(last_split_loads)
            , .load_state   = std::move(current_state)
            , .diagnostics  = diagnostics
        };
    }

}  // namespace timetable::domain::assignment
