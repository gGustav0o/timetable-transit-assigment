#pragma once

#include <utility>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    inline mathfp::Expected<SearchImpedance> make_search_impedance(
        Dimless a_journey_time
        , Dimless a_transfers
        , Dimless a_fare
        , Time transfer_penalty
    ) {
        using validation::ensure_nonneg;
        if (auto r = ensure_nonneg(a_journey_time  , "a_journey_time"  ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(a_transfers     , "a_transfers"     ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(a_fare          , "a_fare"          ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(transfer_penalty, "transfer_penalty"); !r) return mathfp::unexpected(r.error());
        return SearchImpedance{
            .a_journey_time     = a_journey_time
            , .a_transfers      = a_transfers
            , .a_fare           = a_fare
            , .transfer_penalty = transfer_penalty
        };
    }

    inline mathfp::Expected<TransferLimits> make_transfer_limits(
        TransferCount max_transfers
        , Time min_transfer_wait
        , Time max_transfer_wait
        , bool allow_start_wait
        , bool allow_end_wait
    ) {
        if (max_transfers.get() < 0)
            return validation::fail(
                "max_transfers must be non-negative"
                , mathfp::invalid_arg("max_transfers must be non-negative")
                    .ctx("max_transfers", max_transfers.get())
            );

        if (auto r = validation::ensure_nonneg(min_transfer_wait, "min_transfer_wait"); !r)
            return mathfp::unexpected(r.error());

        if (auto r = validation::ensure_nonneg(max_transfer_wait, "max_transfer_wait"); !r)
            return mathfp::unexpected(r.error());

        if (min_transfer_wait.value() > max_transfer_wait.value())
            return validation::fail(
                "min_transfer_wait must be <= max_transfer_wait"
                , mathfp::invalid_arg("min_transfer_wait must be <= max_transfer_wait")
                    .ctx("min_transfer_wait", min_transfer_wait.value())
                    .ctx("max_transfer_wait", max_transfer_wait.value())
            );

        return TransferLimits{
            .max_transfers       = max_transfers
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
        using validation::ensure_nonneg;
        if (auto r = ensure_nonneg(imp_mult, "imp_mult"); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(imp_add , "imp_add" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_mult , "jt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_add  , "jt_add"  ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_mult , "nt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_add  , "nt_add"  ); !r) return mathfp::unexpected(r.error());
        return SearchTolerances{
            .imp_mult  = imp_mult
            , .imp_add = imp_add
            , .jt_mult = jt_mult
            , .jt_add  = jt_add
            , .nt_mult = nt_mult
            , .nt_add  = nt_add
        };
    }

    inline mathfp::Expected<ChoiceTolerances> make_choice_tolerances(
        Dimless imp_mult
        , Dimless imp_add
        , Dimless jt_mult
        , Dimless jt_add
        , Dimless nt_mult
        , Dimless nt_add
    ) {
        using validation::ensure_nonneg;
        if (auto r = ensure_nonneg(imp_mult, "imp_mult"); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(imp_add , "imp_add" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_mult , "jt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_add  , "jt_add"  ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_mult , "nt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_add  , "nt_add"  ); !r) return mathfp::unexpected(r.error());
        return ChoiceTolerances{
            .imp_mult  = imp_mult
            , .imp_add = imp_add
            , .jt_mult = jt_mult
            , .jt_add  = jt_add
            , .nt_mult = nt_mult
            , .nt_add  = nt_add
        };
    }

    inline mathfp::Expected<SplitParams> make_split_params(
        Dimless q_time
        , Dimless q_departure
        , Dimless q_fare
        , Dimless beta
        , Dimless boxcox_t
        , Dimless gamma
        , Dimless x_scale
        , Dimless y_scale
        , Dimless z_scale
    ) {
        using validation::ensure_nonneg;
        using validation::ensure_positive;

        if (auto r = ensure_nonneg(q_time     , "q_time"     ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(q_departure, "q_departure"); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(q_fare     , "q_fare"     ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(boxcox_t   , "boxcox_t"   ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(gamma      , "gamma"      ); !r) return mathfp::unexpected(r.error());

        if (auto r = ensure_positive(beta   , "beta"   ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_positive(x_scale, "x_scale"); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_positive(y_scale, "y_scale"); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_positive(z_scale, "z_scale"); !r) return mathfp::unexpected(r.error());

        return SplitParams{
            .q_time        = q_time
            , .q_departure = q_departure
            , .q_fare      = q_fare
            , .beta        = beta
            , .boxcox_t    = boxcox_t
            , .gamma       = gamma
            , .x_scale     = x_scale
            , .y_scale     = y_scale
            , .z_scale     = z_scale
        };
    }

    inline mathfp::Expected<PreprocessParams> make_preprocess_params(
        WalkCostKind walk_cost_kind
        , WalkCostWeights walk_cost
        , std::optional<Speed> line_speed
        , bool strict_trips
        , bool allow_overnight
        , bool overnight_add_24h
        , bool strict_stop_times
        , TimeAggregationKind time_aggregation
        , bool deduplicate_walk_segments
        , bool stable_ordering
    ) {
        using validation::ensure_nonneg;
        using validation::ensure_positive;

        if (auto r = ensure_nonneg(walk_cost.w_time, "walk_cost.w_time"); !r)
            return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(walk_cost.w_length, "walk_cost.w_length"); !r)
            return mathfp::unexpected(r.error());

        if (walk_cost_kind == WalkCostKind::Weighted) {
            const auto wt = mathfp::units::as_dimless(walk_cost.w_time);
            const auto wl = mathfp::units::as_dimless(walk_cost.w_length);
            if (!(wt > 0.0 || wl > 0.0)) {
                return validation::fail(
                    "weighted walk cost requires at least one positive weight"
                    , mathfp::invalid_arg("weighted walk cost requires at least one positive weight")
                );
            }
        }

        if (line_speed) {
            if (auto r = ensure_positive(*line_speed, "line_speed"); !r)
                return mathfp::unexpected(r.error());
        }

        return PreprocessParams{
            .walk_cost_kind              = walk_cost_kind
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
        , SearchImpedance impedance
        , TransferLimits transfers
        , SearchTolerances search_tolerances
        , ChoiceTolerances choice_tolerances
        , SplitParams split
    ) {
        return SearchParams{
            .preprocess          = std::move(preprocess)
            , .impedance         = std::move(impedance)
            , .transfers         = std::move(transfers)
            , .search_tolerances = std::move(search_tolerances)
            , .choice_tolerances = std::move(choice_tolerances)
            , .split             = std::move(split)
        };
    }

}  // namespace timetable::domain
