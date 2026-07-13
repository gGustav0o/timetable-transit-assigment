#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>

#include "params_txt_codecs.hpp"
#include "params_txt_draft.hpp"

#include "timetable/domain/assignment/capacity_aware_assignment.hpp"
#include "timetable/domain/params/make.hpp"

namespace timetable::infra::params_txt::detail::converters {

    inline mathfp::Expected<timetable::domain::SearchTolerances> to_search_tolerances(
        draft::Tolerance fields
    ) {
        using namespace timetable::domain;

        return make_search_tolerances(
              Dimless{ fields.imp_mult }
            , Dimless{ codecs::temporal_parameter_seconds(fields.imp_add) }
            , Dimless{ fields.jt_mult }
            , Dimless{ codecs::temporal_parameter_seconds(fields.jt_add) }
            , Dimless{ fields.nt_mult }
            , Dimless{ fields.nt_add }
        );
    }

    inline mathfp::Expected<timetable::domain::ChoiceTolerances> to_choice_tolerances(
        draft::Tolerance fields
    ) {
        using namespace timetable::domain;

        return make_choice_tolerances(
              Dimless{ fields.imp_mult }
            , Dimless{ codecs::temporal_parameter_seconds(fields.imp_add) }
            , Dimless{ fields.jt_mult }
            , Dimless{ codecs::temporal_parameter_seconds(fields.jt_add) }
            , Dimless{ fields.nt_mult }
            , Dimless{ fields.nt_add }
        );
    }

    inline mathfp::Expected<timetable::domain::TransferLimits> to_transfer_limits(
        draft::TransferLimits fields
    ) {
        using namespace timetable::domain;

        return make_transfer_limits(
              TransferCount{ static_cast<std::int32_t>(fields.max_transfers) }
            , Time{ codecs::temporal_parameter_seconds(fields.min_transfer_wait) }
            , Time{ codecs::temporal_parameter_seconds(fields.max_transfer_wait) }
            , true
            , true
        );
    }

    inline mathfp::Expected<timetable::domain::SearchImpedance> to_search_impedance(
        draft::SearchImpedance fields
    ) {
        using namespace timetable::domain;

        return make_search_impedance(
              Dimless{ fields.in_vehicle_time }
            , Dimless{ fields.access_time }
            , Dimless{ fields.egress_time }
            , Dimless{ fields.transfer_walk_time }
            , Dimless{ fields.transfer_wait_time }
            , Dimless{ fields.transfer_count }
            , Dimless{ fields.fare }
            , {}
            , Dimless{ fields.volume_capacity_ratio }
        );
    }

    inline mathfp::Expected<timetable::domain::PreprocessParams> default_preprocess_params() {
        using namespace timetable::domain;

        return make_preprocess_params(
              WalkCostKind::Time
            , WalkCostWeights{
                  .w_time   = Dimless{ 1.0 }
                , .w_length = Dimless{ 0.0 }
            }
            , std::nullopt
            , true
            , false
            , true
            , true
            , TimeAggregationKind::Mean
            , true
            , true
        );
    }

    inline mathfp::Expected<timetable::domain::SplitChoiceModel> to_split_choice_model(
        draft::SplitChoiceModel fields
    ) {
        const auto parsed_model =
            timetable::domain::split_choice_model_from_string(fields.choice_model);
        if (!parsed_model.has_value()) {
            return mathfp::unexpected(
                mathfp::invalid_arg("unsupported choiceModel")
                    .ctx("choiceModel", fields.choice_model)
            );
        }
        return *parsed_model;
    }

    inline mathfp::Expected<timetable::domain::SplitChoiceModelConfig>
    to_split_choice_model_config(
          draft::SplitChoiceModel   model_fields
        , draft::ChoiceModelExponent exponent_fields
    ) {
        using namespace timetable::domain;

        MATHFP_TRY_LET(SplitChoiceModel, model, to_split_choice_model(model_fields));
        return SplitChoiceModelConfig{
              .model    = model
            , .exponent = Dimless{ exponent_fields.exponent }
        };
    }

    inline mathfp::Expected<timetable::domain::SplitImpedanceTransformConfig>
    to_split_impedance_transform_config(
          bool                enabled
        , draft::SplitScalars fields
    ) {
        using namespace timetable::domain;

        return SplitImpedanceTransformConfig{
              .boxcox_transform_enabled = enabled
            , .boxcox_t                 = Dimless{ fields.boxcox_t }
        };
    }

    inline mathfp::Expected<timetable::domain::SplitParams> to_split_params(
          draft::SplitImpedance                      split_imp_fields
        , draft::PerceivedJourneyTime                split_pjt_fields
        , draft::SplitIndependence                   indep_fields
        , timetable::domain::SplitChoiceModelConfig  choice_model
        , timetable::domain::SplitImpedanceTransformConfig impedance_transform
    ) {
        using namespace timetable::domain;

        return make_split_params(
              Dimless{ split_imp_fields.time }
            , Dimless{ 1.0 }
            , Dimless{ split_imp_fields.fare }
            , PerceivedJourneyTimeWeights{
                  .in_vehicle_time    = Dimless{ split_pjt_fields.in_vehicle_time }
                , .access_time        = Dimless{ split_pjt_fields.access_time }
                , .egress_time        = Dimless{ split_pjt_fields.egress_time }
                , .transfer_walk_time = Dimless{ split_pjt_fields.transfer_walk_time }
                , .transfer_wait_time = Dimless{ split_pjt_fields.transfer_wait_time }
                , .transfer_count     = Dimless{ split_pjt_fields.transfer_count }
                , .volume_capacity_ratio =
                      Dimless{ split_pjt_fields.volume_capacity_ratio }
            }
            , TemporalUtilityWeights{
                  .early_departure = Dimless{ split_imp_fields.departure_early }
                , .late_departure  = Dimless{ split_imp_fields.departure_late }
            }
            , choice_model
            , impedance_transform
            , SplitIndependenceConfig{
                  .enabled                   = indep_fields.enabled
                , .gamma                     = Dimless{ indep_fields.gamma }
                , .temporal_similarity_scale =
                      Dimless{ indep_fields.temporal_similarity_scale }
                , .higher_quality_scale      =
                      Dimless{ indep_fields.higher_quality_scale }
                , .lower_quality_scale       =
                      Dimless{ indep_fields.lower_quality_scale }
                , .higher_perceived_journey_time_scale =
                      Dimless{ indep_fields.higher_perceived_journey_time_scale }
                , .lower_perceived_journey_time_scale =
                      Dimless{ indep_fields.lower_perceived_journey_time_scale }
                , .higher_fare_scale =
                      Dimless{ indep_fields.higher_fare_scale }
                , .lower_fare_scale =
                      Dimless{ indep_fields.lower_fare_scale }
            }
        );
    }

    inline mathfp::Expected<timetable::domain::assignment::CapacityAwareAssignmentConfig>
    capacity_aware_assignment_config_from_params(
        const timetable::domain::SearchParams& search_params
    ) {
        using namespace timetable::domain;
        using namespace timetable::domain::assignment;

        const auto split_factor = mathfp::units::as_dimless(
            search_params.split.perceived_journey_time.volume_capacity_ratio
        );
        const auto search_factor = mathfp::units::as_dimless(
            search_params.impedance.volume_capacity_ratio
        );
        const auto search_mode =
            search_factor > 0.0
                ? CapacityAwareSearchMode::StoredOnly
                : CapacityAwareSearchMode::Disabled;

        return make_capacity_aware_assignment_config(
              split_factor > 0.0
            , search_mode
            , CapacityPenaltyPolicy::VolumeCapacityRatio
            , CapacityIterationConfig{}
        );
    }

    inline mathfp::Expected<timetable::domain::SearchParams> to_search_params(
        draft::SearchParams fields
    ) {
        using namespace timetable::domain;

        MATHFP_TRY_LET(
              SearchTolerances
            , search_tolerances
            , to_search_tolerances(fields.search_tolerances)
        );
        MATHFP_TRY_LET(
              ChoiceTolerances
            , choice_tolerances
            , to_choice_tolerances(fields.choice_tolerances)
        );
        MATHFP_TRY_LET(
              TransferLimits
            , transfers
            , to_transfer_limits(fields.transfers)
        );
        MATHFP_TRY_LET(
              SearchImpedance
            , impedance
            , to_search_impedance(fields.impedance)
        );
        MATHFP_TRY_LET(
              PreprocessParams
            , preprocess
            , default_preprocess_params()
        );
        MATHFP_TRY_LET(
              SplitChoiceModelConfig
            , choice_model
            , to_split_choice_model_config(
                  fields.split_choice_model
                , fields.split_choice_model_exponent
              )
        );
        MATHFP_TRY_LET(
              SplitImpedanceTransformConfig
            , impedance_transform
            , to_split_impedance_transform_config(
                  fields.split_boxcox_transform_enabled
                , fields.split_scalars
              )
        );
        MATHFP_TRY_LET(
              SplitParams
            , split
            , to_split_params(
                  fields.split_impedance
                , fields.split_perceived_journey_time
                , fields.split_independence
                , choice_model
                , impedance_transform
              )
        );

        return make_search_params(
              std::move(preprocess)
            , std::move(impedance)
            , std::move(transfers)
            , std::move(search_tolerances)
            , std::move(choice_tolerances)
            , std::move(split)
        );
    }

}  // namespace timetable::infra::params_txt::detail::converters
