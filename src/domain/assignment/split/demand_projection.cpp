#include "timetable/domain/assignment/split/demand_projection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool is_non_negative_roundoff(
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

        [[nodiscard]] bool is_significant_probability(
              SplitProbabilityMass probability
            , double threshold
        ) noexcept {
            return probability.get() > threshold;
        }

        [[nodiscard]] bool is_passenger_count_significant(
              SplitPassengerMass passengers
            , SplitDemandMass    total_demand
            , double tolerance
        ) noexcept {
            return passengers.get() > tolerance * total_demand.get();
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_demand_projection_policy(
            const DemandProjectionPolicy& policy
        ) {
            if (!std::isfinite(policy.significant_probability_threshold)
                || policy.significant_probability_threshold < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg(
                        "significant probability threshold must be finite and non-negative"
                    )
                        .ctx(
                              "significant_probability_threshold"
                            , policy.significant_probability_threshold
                        )
                );
            }
            if (!std::isfinite(policy.passenger_count_tolerance)
                || policy.passenger_count_tolerance < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg(
                        "passenger count tolerance must be finite and non-negative"
                    )
                        .ctx("passenger_count_tolerance", policy.passenger_count_tolerance)
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] std::size_t count_significant_alternatives(
              std::span<const SplitProbabilityMass> probabilities
            , std::span<const SplitPassengerMass>   passengers
            , SplitDemandMass                       total_demand
            , double                                probability_threshold
            , double                                passenger_tolerance
        ) noexcept {
            std::size_t count = 0;
            for (std::size_t i = 0; i < probabilities.size(); ++i) {
                if (is_significant_probability(probabilities[i], probability_threshold)
                    || is_passenger_count_significant(
                          passengers[i]
                        , total_demand
                        , passenger_tolerance
                    )) {
                    ++count;
                }
            }
            return count;
        }

    }  // namespace

    mathfp::Expected<DemandProjectionResult> project_demand(
          std::span<const SplitProbabilityMass> probabilities
        , SplitDemandMass                         demand
        , std::size_t                             residual_index
        , const DemandProjectionPolicy&           policy
    ) {
        MATHFP_TRY(validate_demand_projection_policy(policy));

        if (probabilities.empty()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("demand projection requires at least one probability")
            );
        }
        if (residual_index >= probabilities.size()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("demand projection residual index is out of range")
                    .ctx("residual_index", static_cast<std::int64_t>(residual_index))
                    .ctx("probability_count", static_cast<std::int64_t>(probabilities.size()))
            );
        }

        if (!std::isfinite(demand.get()) || demand.get() < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("demand must be finite non-negative")
                    .ctx("demand", demand.get())
            );
        }

        mathfp::CompensatedSum<double> probability_sum;
        for (std::size_t i = 0; i < probabilities.size(); ++i) {
            const auto prob = probabilities[i];
            if (!std::isfinite(prob.get()) || prob.get() < 0.0 || prob.get() > 1.0) {
                return mathfp::unexpected(
                    mathfp::domain_error("probability must be finite and in range [0, 1]")
                        .ctx("index", static_cast<std::int64_t>(i))
                        .ctx("probability", prob.get())
                );
            }
            probability_sum.add(prob.get());
        }

        const double sum = probability_sum.value();
        if (!mathfp::almost_equal(
              sum
            , 1.0
            , mathfp::abs_tolerance(1.0)
            , mathfp::rel_tolerance_coeff<double>()
        )) {
            return mathfp::unexpected(
                mathfp::domain_error("probabilities must sum to 1")
                    .ctx("sum", sum)
            );
        }

        std::vector<SplitPassengerMass> passengers;
        passengers.resize(probabilities.size(), SplitPassengerMass{ 0.0 });

        mathfp::CompensatedSum<double> passenger_prefix;
        for (std::size_t i = 0; i < probabilities.size(); ++i) {
            if (i == residual_index) {
                continue;
            }
            const auto passenger_count = demand.get() * probabilities[i].get();
            if (!std::isfinite(passenger_count) || passenger_count < 0.0) {
                return mathfp::unexpected(
                    mathfp::domain_error(
                        "projected passenger mass must be finite non-negative"
                    )
                        .ctx("index", static_cast<std::int64_t>(i))
                        .ctx("passengers", passenger_count)
                );
            }
            passengers[i] = SplitPassengerMass{ passenger_count };
            passenger_prefix.add(passenger_count);
        }

        auto residual_passengers = demand.get() - passenger_prefix.value();
        if (!is_non_negative_roundoff(residual_passengers, demand.get())) {
            return mathfp::unexpected(
                mathfp::domain_error("invalid residual demand projection")
                    .ctx("residual_passengers", residual_passengers)
                    .ctx("demand", demand.get())
            );
        }
        residual_passengers = std::max(0.0, residual_passengers);
        passengers[residual_index] = SplitPassengerMass{ residual_passengers };

        const auto significant_count = count_significant_alternatives(
              probabilities
            , passengers
            , demand
            , policy.significant_probability_threshold
            , policy.passenger_count_tolerance
        );
        const auto suppressed_count = probabilities.size() - significant_count;

        return DemandProjectionResult{
              .probabilities = std::vector<SplitProbabilityMass>(probabilities.begin(), probabilities.end())
            , .passengers = std::move(passengers)
            , .residual_index = residual_index
            , .significant_alternatives = significant_count
            , .suppressed_alternatives = suppressed_count
        };
    }

}  // namespace timetable::domain::assignment
