#include "timetable/domain/assignment/split/independence.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/summation.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/types/units.hpp>

namespace timetable::domain::assignment {
    namespace {

        [[nodiscard]] bool compared_connection_is_superior(
            double base_quality_advantage
        ) noexcept {
            return base_quality_advantage < 0.0;
        }

        [[nodiscard]] double dimless(Dimless value) noexcept {
            return mathfp::units::as_dimless(value);
        }

        [[nodiscard]] double temporal_similarity(
              SplitIndependenceAlternativeView lhs
            , SplitIndependenceAlternativeView rhs
        ) noexcept {
            return 0.5 * (
                  std::abs(rhs.departure_time - lhs.departure_time)
                + std::abs(rhs.arrival_time - lhs.arrival_time)
            );
        }

        [[nodiscard]] double base_journey_quality_advantage(
              SplitIndependenceAlternativeView lhs
            , SplitIndependenceAlternativeView rhs
        ) noexcept {
            return rhs.perceived_journey_time - lhs.perceived_journey_time;
        }

        [[nodiscard]] double base_fare_quality_advantage(
              SplitIndependenceAlternativeView lhs
            , SplitIndependenceAlternativeView rhs
        ) noexcept {
            return rhs.fare - lhs.fare;
        }

        [[nodiscard]] double asymmetric_scale(
              double  base_quality_advantage
            , Dimless higher_scale
            , Dimless lower_scale
        ) noexcept {
            return compared_connection_is_superior(base_quality_advantage)
                ? dimless(higher_scale)
                : dimless(lower_scale);
        }

        [[nodiscard]] double capped_proximity(
              double similarity
            , double scale
        ) noexcept {
            return 1.0 - std::min(1.0, similarity / scale);
        }

        [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_positive_scale(
              Dimless          value
            , std::string_view name
        ) {
            const auto scale = dimless(value);
            if (!std::isfinite(scale) || scale <= 0.0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("split independence scale must be finite and positive")
                        .ctx("scale", name)
                        .ctx("value", scale)
                );
            }
            return mathfp::kUnit;
        }

        [[nodiscard]] SplitIndependenceInfluence split_connection_influence_unchecked(
              const SplitIndependenceConfig&   config
            , SplitIndependenceAlternativeView base
            , SplitIndependenceAlternativeView other
        ) noexcept {
            const auto x = temporal_similarity(base, other);
            const auto y = base_journey_quality_advantage(base, other);
            const auto z = base_fare_quality_advantage(base, other);
            const auto proximity = capped_proximity(x, dimless(config.temporal_similarity_scale));
            const auto s_y = asymmetric_scale(
                  y
                , config.higher_perceived_journey_time_scale
                , config.lower_perceived_journey_time_scale
            );
            const auto s_z = asymmetric_scale(
                  z
                , config.higher_fare_scale
                , config.lower_fare_scale
            );

            const auto quality_distance = std::min(
                  1.0
                , (s_z * std::abs(y) + s_y * std::abs(z)) / (s_y * s_z)
            );
            const auto quality_factor = 1.0 - dimless(config.gamma) * quality_distance;
            return SplitIndependenceInfluence{
                proximity * std::max(0.0, quality_factor)
            };
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> validate_split_independence(
        SplitIndependenceWeight independence
    ) {
        if (!std::isfinite(independence.get()) || independence.get() <= 0.0
            || independence.get() > 1.0) {
            return mathfp::unexpected(
                mathfp::domain_error(
                    "split independence must be finite and in range (0, 1]"
                )
                    .ctx("independence", independence.get())
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_split_independence_view(
          SplitIndependenceAlternativeView view
        , std::size_t                      index
    ) {
        if (!std::isfinite(view.departure_time)
            || !std::isfinite(view.arrival_time)
            || !std::isfinite(view.perceived_journey_time)
            || !std::isfinite(view.fare)) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split independence alternative contains non-finite value")
                    .ctx("index", static_cast<std::int64_t>(index))
                    .ctx("departure_time", view.departure_time)
                    .ctx("arrival_time", view.arrival_time)
                    .ctx("perceived_journey_time", view.perceived_journey_time)
                    .ctx("fare", view.fare)
            );
        }
        return mathfp::kUnit;
    }

    mathfp::Expected<mathfp::Unit> validate_split_independence_config(
        const SplitIndependenceConfig& config
    ) {
        const auto gamma = dimless(config.gamma);
        if (!std::isfinite(gamma) || gamma < 0.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split independence gamma must be finite and non-negative")
                    .ctx("gamma", gamma)
            );
        }

        MATHFP_TRY(validate_positive_scale(
              config.temporal_similarity_scale
            , "temporal_similarity_scale"
        ));
        MATHFP_TRY(validate_positive_scale(
              config.higher_perceived_journey_time_scale
            , "higher_perceived_journey_time_scale"
        ));
        MATHFP_TRY(validate_positive_scale(
              config.lower_perceived_journey_time_scale
            , "lower_perceived_journey_time_scale"
        ));
        MATHFP_TRY(validate_positive_scale(config.higher_fare_scale, "higher_fare_scale"));
        MATHFP_TRY(validate_positive_scale(config.lower_fare_scale, "lower_fare_scale"));

        return mathfp::kUnit;
    }

    mathfp::Expected<SplitIndependenceInfluence> split_connection_influence(
          const SplitIndependenceConfig&   config
        , SplitIndependenceAlternativeView base
        , SplitIndependenceAlternativeView other
    ) {
        MATHFP_TRY(validate_split_independence_config(config));
        MATHFP_TRY(validate_split_independence_view(base, 0u));
        MATHFP_TRY(validate_split_independence_view(other, 1u));

        return split_connection_influence_unchecked(config, base, other);
    }

    mathfp::Expected<SplitIndependenceWeight> compute_split_independence(
          const SplitIndependenceConfig&                   config
        , std::span<const SplitIndependenceAlternativeView> alternatives
        , std::size_t                                      index
    ) {
        if (index >= alternatives.size()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("split independence index is out of range")
                    .ctx("index", static_cast<std::int64_t>(index))
                    .ctx("alternatives", static_cast<std::int64_t>(alternatives.size()))
            );
        }

        for (std::size_t i = 0; i < alternatives.size(); ++i) {
            MATHFP_TRY(validate_split_independence_view(alternatives[i], i));
        }

        if (!config.enabled) {
            return SplitIndependenceWeight{ 1.0 };
        }
        MATHFP_TRY(validate_split_independence_config(config));

        mathfp::CompensatedSum<double> influence_sum;
        for (std::size_t i = 0; i < alternatives.size(); ++i) {
            if (i == index) {
                continue;
            }
            const auto influence = split_connection_influence_unchecked(
                  config
                , alternatives[index]
                , alternatives[i]
            );
            influence_sum.add(influence.get());
        }

        const auto independence = SplitIndependenceWeight{
            1.0 / (1.0 + influence_sum.value())
        };
        MATHFP_TRY(validate_split_independence(independence));
        return independence;
    }

    mathfp::Expected<std::vector<SplitIndependenceWeight>>
    compute_split_independences(
          const SplitIndependenceConfig&                   config
        , std::span<const SplitIndependenceAlternativeView> alternatives
    ) {
        std::vector<SplitIndependenceWeight> result;
        result.reserve(alternatives.size());
        for (std::size_t i = 0; i < alternatives.size(); ++i) {
            MATHFP_TRY_LET(SplitIndependenceWeight, independence,
                compute_split_independence(config, alternatives, i));
            result.push_back(independence);
        }
        return result;
    }

}  // namespace timetable::domain::assignment
