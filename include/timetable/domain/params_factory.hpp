#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    namespace detail {

        inline mathfp::Expected<mathfp::Unit> ensure_nonnegative_search_impedance_inputs(
            const SearchImpedance& impedance
        ) {
            MATHFP_TRY(validation::ensure_nonneg(impedance.in_vehicle_time    , "search_impedance.in_vehicle_time"));
            MATHFP_TRY(validation::ensure_nonneg(impedance.access_time        , "search_impedance.access_time"));
            MATHFP_TRY(validation::ensure_nonneg(impedance.egress_time        , "search_impedance.egress_time"));
            MATHFP_TRY(validation::ensure_nonneg(impedance.transfer_walk_time , "search_impedance.transfer_walk_time"));
            MATHFP_TRY(validation::ensure_nonneg(impedance.transfer_wait_time , "search_impedance.transfer_wait_time"));
            MATHFP_TRY(validation::ensure_nonneg(impedance.transfer_count     , "search_impedance.transfer_count"));
            MATHFP_TRY(validation::ensure_nonneg(impedance.fare               , "search_impedance.fare"));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_nonnegative_tolerance_inputs(
              Dimless   imp_mult
            , Dimless imp_add
            , Dimless jt_mult
            , Dimless jt_add
            , Dimless nt_mult
            , Dimless nt_add
        ) {
            MATHFP_TRY(validation::ensure_nonneg(imp_mult, "imp_mult"));
            MATHFP_TRY(validation::ensure_nonneg(imp_add , "imp_add"));
            MATHFP_TRY(validation::ensure_nonneg(jt_mult , "jt_mult"));
            MATHFP_TRY(validation::ensure_nonneg(jt_add  , "jt_add"));
            MATHFP_TRY(validation::ensure_nonneg(nt_mult , "nt_mult"));
            MATHFP_TRY(validation::ensure_nonneg(nt_add  , "nt_add"));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_nonnegative_split_weights(
              Dimless   q_time
            , Dimless q_departure
            , Dimless q_fare
            , Dimless boxcox_t
            , Dimless gamma
        ) {
            MATHFP_TRY(validation::ensure_nonneg(q_time     , "q_time"));
            MATHFP_TRY(validation::ensure_nonneg(q_departure, "q_departure"));
            MATHFP_TRY(validation::ensure_nonneg(q_fare     , "q_fare"));
            MATHFP_TRY(validation::ensure_nonneg(boxcox_t   , "boxcox_t"));
            MATHFP_TRY(validation::ensure_nonneg(gamma      , "gamma"));
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
              Dimless   beta
            , Dimless temporal_similarity_scale
            , Dimless higher_quality_scale
            , Dimless lower_quality_scale
        ) {
            MATHFP_TRY(validation::ensure_positive(beta, "beta"));
            MATHFP_TRY(validation::ensure_positive(
                temporal_similarity_scale, "temporal_similarity_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                higher_quality_scale, "higher_quality_scale"
            ));
            MATHFP_TRY(validation::ensure_positive(
                lower_quality_scale, "lower_quality_scale"
            ));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_walk_cost_weights_nonnegative(
            WalkCostWeights walk_cost
        ) {
            MATHFP_TRY(validation::ensure_nonneg(walk_cost.w_time, "walk_cost.w_time"));
            MATHFP_TRY(validation::ensure_nonneg(walk_cost.w_length, "walk_cost.w_length"));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_weighted_walk_cost_has_positive_weight(
              WalkCostKind      walk_cost_kind
            , WalkCostWeights walk_cost
        ) {
            if (walk_cost_kind != WalkCostKind::Weighted) {
                return mathfp::kUnit;
            }

            const auto wt = mathfp::units::as_dimless(walk_cost.w_time);
            const auto wl = mathfp::units::as_dimless(walk_cost.w_length);
            if (!(wt > 0.0 || wl > 0.0)) {
                const char* message = "weighted walk cost requires at least one positive weight";
                return validation::fail(message, mathfp::invalid_arg(message));
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_optional_line_speed_positive(
            const std::optional<Speed>& line_speed
        ) {
            if (line_speed) {
                MATHFP_TRY(validation::ensure_positive(*line_speed, "line_speed"));
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_max_transfers_nonnegative(
            TransferCount max_transfers
        ) {
            if (max_transfers.get() < 0) {
                const char* message = "max_transfers must be non-negative";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message)
                        .ctx("max_transfers", max_transfers.get())
                );
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_transfer_waits_ordered(
              Time   min_transfer_wait
            , Time max_transfer_wait
        ) {
            if (min_transfer_wait.value() > max_transfer_wait.value()) {
                const char* message = "min_transfer_wait must be <= max_transfer_wait";
                return validation::fail(
                      message
                    , mathfp::invalid_arg(message)
                        .ctx("min_transfer_wait", min_transfer_wait.value())
                        .ctx("max_transfer_wait", max_transfer_wait.value())
                );
            }
            return mathfp::kUnit;
        }

        template <class Tolerance>
        inline mathfp::Expected<Tolerance> make_tolerance_bundle(
              Dimless   imp_mult
            , Dimless imp_add
            , Dimless jt_mult
            , Dimless jt_add
            , Dimless nt_mult
            , Dimless nt_add
        ) {
            MATHFP_TRY(ensure_nonnegative_tolerance_inputs(
                imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add
            ));
            return Tolerance{
                  .imp_mult = imp_mult
                , .imp_add  = imp_add
                , .jt_mult  = jt_mult
                , .jt_add   = jt_add
                , .nt_mult  = nt_mult
                , .nt_add   = nt_add
            };
        }

    }  // namespace detail

    inline mathfp::Expected<SearchImpedance> make_search_impedance(
          Dimless             in_vehicle_time
        , Dimless           access_time
        , Dimless           egress_time
        , Dimless           transfer_walk_time
        , Dimless           transfer_wait_time
        , Dimless           transfer_count
        , Dimless           fare
        , FareNormalization fare_normalization = {}
    ) {
        SearchImpedance impedance{
              .in_vehicle_time    = in_vehicle_time
            , .access_time        = access_time
            , .egress_time        = egress_time
            , .transfer_walk_time = transfer_walk_time
            , .transfer_wait_time = transfer_wait_time
            , .transfer_count     = transfer_count
            , .fare               = fare
            , .fare_normalization = fare_normalization
        };
        MATHFP_TRY(detail::ensure_nonnegative_search_impedance_inputs(impedance));
        return impedance;
    }

    inline mathfp::Expected<TransferLimits> make_transfer_limits(
          TransferCount max_transfers
        , Time          min_transfer_wait
        , Time          max_transfer_wait
        , bool          allow_start_wait
        , bool          allow_end_wait
    ) {
        MATHFP_TRY(detail::ensure_max_transfers_nonnegative(max_transfers));
        MATHFP_TRY(validation::ensure_nonneg(min_transfer_wait, "min_transfer_wait"));
        MATHFP_TRY(validation::ensure_nonneg(max_transfer_wait, "max_transfer_wait"));
        MATHFP_TRY(detail::ensure_transfer_waits_ordered(
            min_transfer_wait, max_transfer_wait
        ));

        return TransferLimits{
              .max_transfers     = max_transfers
            , .min_transfer_wait = min_transfer_wait
            , .max_transfer_wait = max_transfer_wait
            , .allow_start_wait  = allow_start_wait
            , .allow_end_wait    = allow_end_wait
        };
    }

    inline mathfp::Expected<SearchTolerances> make_search_tolerances(
          Dimless imp_mult
        , Dimless imp_add
        , Dimless jt_mult
        , Dimless jt_add
        , Dimless nt_mult
        , Dimless nt_add
    ) {
        return detail::make_tolerance_bundle<SearchTolerances>(
            imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add
        );
    }

    inline mathfp::Expected<ChoiceTolerances> make_choice_tolerances(
          Dimless imp_mult
        , Dimless imp_add
        , Dimless jt_mult
        , Dimless jt_add
        , Dimless nt_mult
        , Dimless nt_add
    ) {
        return detail::make_tolerance_bundle<ChoiceTolerances>(
            imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add
        );
    }

    inline mathfp::Expected<SplitParams> make_split_params(
          Dimless                     q_time
        , Dimless                     q_departure
        , Dimless                     q_fare
        , PerceivedJourneyTimeWeights perceived_journey_time
        , TemporalUtilityWeights      temporal_utility
        , Dimless                     beta
        , Dimless                     boxcox_t
        , Dimless                     gamma
        , Dimless                     temporal_similarity_scale
        , Dimless                     higher_quality_scale
        , Dimless                     lower_quality_scale
    ) {
        MATHFP_TRY(detail::ensure_nonnegative_split_weights(
            q_time, q_departure, q_fare, boxcox_t, gamma
        ));
        MATHFP_TRY(detail::ensure_nonnegative_perceived_journey_time_weights(
            perceived_journey_time
        ));
        MATHFP_TRY(detail::ensure_nonnegative_temporal_utility_weights(
            temporal_utility
        ));
        MATHFP_TRY(detail::ensure_positive_split_scales(
              beta
            , temporal_similarity_scale
            , higher_quality_scale
            , lower_quality_scale
        ));

        return SplitParams{
              .q_time                    = q_time
            , .q_departure               = q_departure
            , .q_fare                    = q_fare
            , .perceived_journey_time    = perceived_journey_time
            , .temporal_utility          = temporal_utility
            , .beta                      = beta
            , .boxcox_t                  = boxcox_t
            , .gamma                     = gamma
            , .temporal_similarity_scale = temporal_similarity_scale
            , .higher_quality_scale      = higher_quality_scale
            , .lower_quality_scale       = lower_quality_scale
        };
    }

    inline mathfp::Expected<PreprocessParams> make_preprocess_params(
          WalkCostKind         walk_cost_kind
        , WalkCostWeights      walk_cost
        , std::optional<Speed> line_speed
        , bool                 strict_trips
        , bool                 allow_overnight
        , bool                 overnight_add_24h
        , bool                 strict_stop_times
        , TimeAggregationKind  time_aggregation
        , bool                 deduplicate_walk_segments
        , bool                 stable_ordering
    ) {
        MATHFP_TRY(detail::ensure_walk_cost_weights_nonnegative(walk_cost));
        MATHFP_TRY(detail::ensure_weighted_walk_cost_has_positive_weight(
            walk_cost_kind, walk_cost
        ));
        MATHFP_TRY(detail::ensure_optional_line_speed_positive(line_speed));

        return PreprocessParams{
              .walk_cost_kind            = walk_cost_kind
            , .walk_cost                 = walk_cost
            , .line_speed                = line_speed
            , .strict_trips              = strict_trips
            , .allow_overnight           = allow_overnight
            , .overnight_add_24h         = overnight_add_24h
            , .strict_stop_times         = strict_stop_times
            , .time_aggregation          = time_aggregation
            , .deduplicate_walk_segments = deduplicate_walk_segments
            , .stable_ordering           = stable_ordering
        };
    }

    inline mathfp::Expected<SearchParams> make_search_params(
          PreprocessParams preprocess
        , SearchImpedance  impedance
        , TransferLimits   transfers
        , SearchTolerances search_tolerances
        , ChoiceTolerances choice_tolerances
        , SplitParams      split
    ) {
        return SearchParams{
              .preprocess        = std::move(preprocess)
            , .impedance         = std::move(impedance)
            , .transfers         = std::move(transfers)
            , .search_tolerances = std::move(search_tolerances)
            , .choice_tolerances = std::move(choice_tolerances)
            , .split             = std::move(split)
        };
    }

}  // namespace timetable::domain
