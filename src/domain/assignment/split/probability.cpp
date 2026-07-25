#include "timetable/domain/assignment/split/probability.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>

#include "timetable/domain/assignment/split/demand_projection.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] std::size_t max_weight_index(
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

        [[nodiscard]] bool is_non_negative_roundoff(
              double value
            , double scale
            , double rel_tolerance_coeff
        ) noexcept {
            return value >= 0.0
                || mathfp::almost_equal(
                      value
                    , 0.0
                    , mathfp::abs_tolerance(scale)
                    , rel_tolerance_coeff
                );
        }

        [[nodiscard]] bool is_numerically_significant_share(
              SplitProbabilityMass probability
            , SplitPassengerMass   passengers
            , SplitDemandMass      demand_passengers
            , double               probability_threshold
        ) noexcept {
            return probability.get() > probability_threshold
                && passengers.get()  > probability_threshold * demand_passengers.get();
        }

        [[nodiscard]] std::size_t compact_numerical_support(
              std::vector<SplitProbabilityMass>& probabilities
            , std::vector<SplitPassengerMass>&   passengers
            , std::size_t                        residual_index
            , SplitDemandMass                    demand_passengers
            , double                             suppression_threshold
        ) noexcept {
            mathfp::CompensatedSum<double> suppressed_probability;
            mathfp::CompensatedSum<double> suppressed_passengers;
            std::size_t suppressed_count = 0;

            for (std::size_t i = 0; i < probabilities.size(); ++i) {
                if (i == residual_index) {
                    continue;
                }
                if (is_numerically_significant_share(
                      probabilities[i]
                    , passengers[i]
                    , demand_passengers
                    , suppression_threshold
                )) {
                    continue;
                }

                suppressed_probability.add(probabilities[i].get());
                suppressed_passengers .add(passengers[i].get());
                probabilities[i] = SplitProbabilityMass{ 0.0 };
                passengers[i]    = SplitPassengerMass{ 0.0 };
                ++suppressed_count;
            }

            probabilities[residual_index] = SplitProbabilityMass{
                probabilities[residual_index].get() + suppressed_probability.value()
            };
            passengers[residual_index] = SplitPassengerMass{
                passengers[residual_index].get() + suppressed_passengers.value()
            };
            return suppressed_count;
        }

    }  // namespace

    mathfp::Expected<SplitAllocation> normalize_split_log_weights(
          std::span<const SplitLogWeight> log_weights
        , SplitDemandMass                 demand_passengers
        , const ProbabilityPolicy&         policy
    ) {
        if (log_weights.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg(
                    "split log-weight normalization requires at least one alternative"
                )
            );
        }
        if (!std::isfinite(demand_passengers.get()) || demand_passengers.get() < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg(
                    "split log-weight normalization requires finite non-negative demand"
                )
                    .ctx("demand_passengers", demand_passengers.get())
            );
        }

        double max_log_weight = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < log_weights.size(); ++i) {
            if (!std::isfinite(log_weights[i].get())) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split log weight must be finite")
                        .ctx("index", static_cast<std::int64_t>(i))
                        .ctx("log_weight", log_weights[i].get())
                );
            }
            max_log_weight = std::max(max_log_weight, log_weights[i].get());
        }

        std::vector<double> weights;
        weights.reserve(log_weights.size());
        for (const auto log_weight : log_weights) {
            weights.push_back(std::exp(log_weight.get() - max_log_weight));
        }
        const auto weight_sum = mathfp::compensated_sum(weights);
        if (!(weight_sum > 0.0) || !std::isfinite(weight_sum)) {
            return mathfp::unexpected(
                mathfp::domain_error("invalid split weight normalization")
            );
        }

        const auto residual_index = max_weight_index(weights);
        std::vector<SplitProbabilityMass> probabilities(
              log_weights.size()
            , SplitProbabilityMass{ 0.0 }
        );

        mathfp::CompensatedSum<double> probability_prefix;
        for (std::size_t i = 0; i < log_weights.size(); ++i) {
            if (i == residual_index) {
                continue;
            }
            const auto probability = weights[i] / weight_sum;
            probabilities[i] = SplitProbabilityMass{ probability };
            probability_prefix.add(probability);
        }

        auto residual_probability = 1.0 - probability_prefix.value();
        if (!is_non_negative_roundoff(
              residual_probability
            , 1.0
            , policy.relative_tolerance_coefficient
        )) {
            return mathfp::unexpected(
                mathfp::domain_error("invalid residual split normalization")
                    .ctx("residual_probability", residual_probability)
            );
        }

        residual_probability = std::max(0.0, residual_probability);
        probabilities[residual_index] = SplitProbabilityMass{ residual_probability };

        MATHFP_TRY_LET(
              DemandProjectionResult
            , projection
            , project_demand(
                  std::span<const SplitProbabilityMass>{
                      probabilities.data()
                    , probabilities.size()
                  }
                , demand_passengers
                , residual_index
            )
        );
        auto passengers = std::move(projection.passengers);

        const auto suppressed = compact_numerical_support(
              probabilities
            , passengers
            , residual_index
            , demand_passengers
            , policy.numerical_suppression_threshold
        );

        return SplitAllocation{
              .probabilities = std::move(probabilities)
            , .passengers = std::move(passengers)
            , .residual_index = residual_index
            , .suppressed_numerical_shares = suppressed
        };
    }

}  // namespace timetable::domain::assignment
