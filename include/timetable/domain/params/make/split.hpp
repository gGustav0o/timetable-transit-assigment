#pragma once

#include <cstdint>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params/split.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    namespace params_detail {

        inline mathfp::Expected<mathfp::Unit> ensure_nonnegative_split_weights(
              Dimless q_time
            , Dimless q_departure
            , Dimless q_fare
        ) {
            MATHFP_TRY(validation::ensure_nonneg(q_time     , "q_time"));
            MATHFP_TRY(validation::ensure_nonneg(q_departure, "q_departure"));
            MATHFP_TRY(validation::ensure_nonneg(q_fare     , "q_fare"));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_split_choice_model_config(
            SplitChoiceModelConfig config
        ) {
            switch (config.model) {
                case SplitChoiceModel::Kirchhoff:
                case SplitChoiceModel::Logit:
                case SplitChoiceModel::Lohse:
                case SplitChoiceModel::BoxCox:
                    break;
                default: {
                    const char* message = "unsupported split choice model";
                    return validation::fail(
                          message
                        , mathfp::invalid_arg(message)
                            .ctx("choice_model", static_cast<std::int64_t>(config.model))
                    );
                }
            }

            MATHFP_TRY(validation::ensure_positive(
                config.exponent, "split.choice_model.exponent"
            ));

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_split_impedance_transform_config(
            SplitImpedanceTransformConfig config
        ) {
            const auto boxcox_t = mathfp::units::as_dimless(config.boxcox_t);
            if (!validation::is_finite(boxcox_t)) {
                const char* message = "Box-Cox transform parameter is not finite";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message)
                        .ctx(
                              "boxcox_transform_enabled"
                            , config.boxcox_transform_enabled ? "true" : "false"
                          )
                        .ctx("boxcox_t", boxcox_t)
                );
            }

            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_nonnegative_perceived_journey_time_weights(
            PerceivedJourneyTimeWeights weights
        ) {
            MATHFP_TRY(validation::ensure_nonneg(
                weights.in_vehicle_time, "perceived_journey_time.in_vehicle_time"
            ));
            MATHFP_TRY(validation::ensure_nonneg(
                weights.access_time, "perceived_journey_time.access_time"
            ));
            MATHFP_TRY(validation::ensure_nonneg(
                weights.egress_time, "perceived_journey_time.egress_time"
            ));
            MATHFP_TRY(validation::ensure_nonneg(
                weights.transfer_walk_time, "perceived_journey_time.transfer_walk_time"
            ));
            MATHFP_TRY(validation::ensure_nonneg(
                weights.transfer_wait_time, "perceived_journey_time.transfer_wait_time"
            ));
            MATHFP_TRY(validation::ensure_nonneg(
                weights.transfer_count, "perceived_journey_time.transfer_count"
            ));
            MATHFP_TRY(validation::ensure_nonneg(
                weights.volume_capacity_ratio, "perceived_journey_time.volume_capacity_ratio"
            ));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_nonnegative_temporal_utility_weights(
            TemporalUtilityWeights weights
        ) {
            MATHFP_TRY(validation::ensure_nonneg(
                weights.early_departure, "temporal_utility.early_departure"
            ));
            MATHFP_TRY(validation::ensure_nonneg(
                weights.late_departure, "temporal_utility.late_departure"
            ));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_positive_split_scales(
              Dimless temporal_similarity_scale
            , Dimless higher_quality_scale
            , Dimless lower_quality_scale
            , Dimless higher_perceived_journey_time_scale
            , Dimless lower_perceived_journey_time_scale
            , Dimless higher_fare_scale
            , Dimless lower_fare_scale
        ) {
            MATHFP_TRY(validation::ensure_positive(
                temporal_similarity_scale, "split.independence.temporal_similarity_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                higher_quality_scale, "split.independence.higher_quality_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                lower_quality_scale, "split.independence.lower_quality_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                higher_perceived_journey_time_scale,
                "split.independence.higher_perceived_journey_time_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                lower_perceived_journey_time_scale,
                "split.independence.lower_perceived_journey_time_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                higher_fare_scale, "split.independence.higher_fare_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                lower_fare_scale, "split.independence.lower_fare_scale"
            ));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_split_independence_config(
            SplitIndependenceConfig config
        ) {
            MATHFP_TRY(validation::ensure_nonneg(
                config.gamma, "split.independence.gamma"
            ));
            MATHFP_TRY(ensure_positive_split_scales(
                  config.temporal_similarity_scale
                , config.higher_quality_scale
                , config.lower_quality_scale
                , config.higher_perceived_journey_time_scale
                , config.lower_perceived_journey_time_scale
                , config.higher_fare_scale
                , config.lower_fare_scale
            ));
            return mathfp::kUnit;
        }

    }  // namespace params_detail

    inline mathfp::Expected<SplitParams> make_split_params(
          Dimless                     q_time
        , Dimless                     q_departure
        , Dimless                     q_fare
        , PerceivedJourneyTimeWeights perceived_journey_time
        , TemporalUtilityWeights      temporal_utility
        , SplitChoiceModelConfig      choice_model
        , SplitImpedanceTransformConfig impedance_transform
        , SplitIndependenceConfig     independence
    ) {
        MATHFP_TRY(params_detail::ensure_nonnegative_split_weights(
            q_time, q_departure, q_fare
        ));
        MATHFP_TRY(params_detail::ensure_split_choice_model_config(choice_model));
        MATHFP_TRY(params_detail::ensure_split_impedance_transform_config(
            impedance_transform
        ));
        MATHFP_TRY(params_detail::ensure_nonnegative_perceived_journey_time_weights(
            perceived_journey_time
        ));
        MATHFP_TRY(params_detail::ensure_nonnegative_temporal_utility_weights(
            temporal_utility
        ));
        MATHFP_TRY(params_detail::ensure_split_independence_config(independence));

        return SplitParams{
              .q_time                    = q_time
            , .q_departure               = q_departure
            , .q_fare                    = q_fare
            , .perceived_journey_time    = perceived_journey_time
            , .temporal_utility          = temporal_utility
            , .choice_model              = choice_model
            , .impedance_transform       = impedance_transform
            , .independence              = independence
        };
    }

}  // namespace timetable::domain
