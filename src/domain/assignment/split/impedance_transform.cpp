#include "timetable/domain/assignment/split/impedance_transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

#include <mathfp/core/error.hpp>
#include <mathfp/core/numeric_tolerance.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/numeric.hpp"

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] double dimless(Dimless value) noexcept {
            return mathfp::units::as_dimless(value);
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_finite_nonnegative(
              double           value
            , std::string_view name
        ) {
            if (!std::isfinite(value) || value < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split impedance parameter must be finite and non-negative")
                        .ctx("parameter", name)
                        .ctx("value", value)
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_temporal_utility_policy(
            const SplitTemporalUtilityPolicy& policy
        ) {
            MATHFP_TRY(validate_finite_nonnegative(
                  dimless(policy.weights.early_departure)
                , "temporal_utility.early_departure"
            ));
            MATHFP_TRY(validate_finite_nonnegative(
                  dimless(policy.weights.late_departure)
                , "temporal_utility.late_departure"
            ));
            return mathfp::kUnit;
        }

        [[nodiscard]] mathfp::Expected<SplitTransformedImpedance> identity_transform(
            SplitRawImpedance impedance
        ) {
            if (!std::isfinite(impedance.get()) || impedance.get() < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split impedance must be finite and non-negative")
                        .ctx("impedance", impedance.get())
                );
            }
            return SplitTransformedImpedance{
                std::max(impedance.get(), numeric::positive_stability_floor())
            };
        }

        [[nodiscard]] mathfp::Expected<SplitTransformedImpedance> box_cox_transform(
              SplitRawImpedance impedance
            , double t
        ) {
            if (!std::isfinite(impedance.get()) || impedance.get() < 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split impedance must be finite and non-negative")
                        .ctx("impedance", impedance.get())
                );
            }
            if (!std::isfinite(t)) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("Box-Cox t parameter must be finite")
                        .ctx("t", t)
                );
            }

            const auto positive = std::max(
                impedance.get(),
                numeric::positive_stability_floor()
            );
            const auto transformed = mathfp::almost_zero(t)
                ? std::log(positive)
                : (std::pow(positive, t) - 1.0) / t;

            if (!std::isfinite(transformed)) {
                return mathfp::unexpected(
                    mathfp::domain_error("Box-Cox transformation produced non-finite value")
                        .ctx("impedance", impedance.get())
                        .ctx("t", t)
                        .ctx("transformed", transformed)
                );
            }

            return SplitTransformedImpedance{ transformed };
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_split_impedance_view(
          SplitImpedanceAlternativeView view
        , std::size_t                   index
    ) {
        if (!std::isfinite(view.departure_time.value())
            || !std::isfinite(view.arrival_time.value())
            || !std::isfinite(view.perceived_journey_time)
            || !std::isfinite(view.fare)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split impedance alternative contains non-finite value")
                    .ctx("index", static_cast<std::int64_t>(index))
                    .ctx("departure_time", view.departure_time.value())
                    .ctx("arrival_time", view.arrival_time.value())
                    .ctx("perceived_journey_time", view.perceived_journey_time)
                    .ctx("fare", view.fare)
            );
        }
        if (view.perceived_journey_time < 0.0 || view.fare < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split impedance alternative must be non-negative")
                    .ctx("index", static_cast<std::int64_t>(index))
                    .ctx("perceived_journey_time", view.perceived_journey_time)
                    .ctx("fare", view.fare)
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_split_impedance_policy(
        const SplitImpedancePolicy& policy
    ) {
        MATHFP_TRY(validate_finite_nonnegative(dimless(policy.q_time), "q_time"));
        MATHFP_TRY(validate_finite_nonnegative(
              dimless(policy.q_departure)
            , "q_departure"
        ));
        MATHFP_TRY(validate_finite_nonnegative(dimless(policy.q_fare), "q_fare"));
        MATHFP_TRY(validate_temporal_utility_policy(policy.temporal_utility));
        return mathfp::kUnit;
    }

    Time split_reference_time(
          SplitImpedanceAlternativeView alternative
        , SplitReferenceTimeBasis       basis
    ) noexcept {
        switch (basis) {
            case SplitReferenceTimeBasis::Departure:
                return alternative.departure_time;
            case SplitReferenceTimeBasis::Arrival:
                return alternative.arrival_time;
        }
        return alternative.departure_time;
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

    mathfp::Expected<double> split_temporal_utility(
          SplitImpedanceAlternativeView alternative
        , const TimeInterval&           interval
        , const SplitTemporalUtilityPolicy& policy
    ) {
        MATHFP_TRY(validate_split_impedance_view(alternative, 0u));
        MATHFP_TRY(validate_temporal_utility_policy(policy));

        const auto reference_time = split_reference_time(alternative, policy.basis);
        const auto utility =
              dimless(policy.weights.early_departure)
                * split_early_deviation(reference_time, interval)
            + dimless(policy.weights.late_departure)
                * split_late_deviation(reference_time, interval);
        if (!std::isfinite(utility)) {
            return mathfp::unexpected(
                mathfp::domain_error("split temporal utility produced non-finite value")
                    .ctx("temporal_utility", utility)
            );
        }
        return utility;
    }

    mathfp::Expected<SplitRawImpedance> compute_split_impedance(
          SplitImpedanceAlternativeView alternative
        , const TimeInterval&           interval
        , const SplitImpedancePolicy&   policy
    ) {
        MATHFP_TRY(validate_split_impedance_view(alternative, 0u));
        MATHFP_TRY(validate_split_impedance_policy(policy));
        MATHFP_TRY_LET(
              double
            , temporal_utility
            , split_temporal_utility(
                  alternative
                , interval
                , policy.temporal_utility
            )
        );

        const auto impedance =
              dimless(policy.q_time) * alternative.perceived_journey_time
            + dimless(policy.q_departure) * temporal_utility
            + dimless(policy.q_fare) * alternative.fare;
        if (!std::isfinite(impedance) || impedance < 0.0) {
            return mathfp::unexpected(
                mathfp::domain_error(
                    "split impedance must be finite and non-negative"
                )
                    .ctx("split_impedance", impedance)
                    .ctx("perceived_journey_time", alternative.perceived_journey_time)
                    .ctx("temporal_utility", temporal_utility)
                    .ctx("fare", alternative.fare)
            );
        }

        return SplitRawImpedance{ impedance };
    }

    mathfp::Expected<SplitTransformedImpedance> apply_impedance_transform(
          SplitRawImpedance original_impedance
        , const SplitImpedanceTransformPolicy& policy
    ) {
        if (!policy.config.boxcox_transform_enabled) {
            return identity_transform(original_impedance);
        }
        return box_cox_transform(
            original_impedance,
            mathfp::units::as_dimless(policy.config.boxcox_t)
        );
    }

}  // namespace timetable::domain::assignment
