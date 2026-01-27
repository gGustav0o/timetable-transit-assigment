#pragma once

#include <cassert>
#include <cmath>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>
#include <mathfp/types/units.hpp>

#include "timetable/domain/params.hpp"

namespace timetable::domain {

    namespace detail {

        inline bool is_finite(double v) noexcept {
            return std::isfinite(v);
        }

        inline mathfp::Unexpected fail(
            const char* assert_msg
            , mathfp::Error err
        ) {
            assert(false && "validation failed");
            return mathfp::unexpected(std::move(err).ctx("assert", assert_msg));
        }

        inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
            Dimless v, const char* name
        ) {
            const auto x = mathfp::units::as_dimless(v);
            if (!is_finite(x))
                return fail(
                    "coefficient is not finite"
                    , mathfp::invalid_arg("coefficient is not finite").ctx("name", name)
                );

            if (x < 0.0)
                return fail(
                    "coefficient must be non-negative"
                    , mathfp::invalid_arg("coefficient must be non-negative").ctx("name", name)
                );
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_positive(
            Dimless v, const char* name
        ) {
            const auto x = mathfp::units::as_dimless(v);
            if (!is_finite(x))
                return fail(
                    "coefficient is not finite"
                    , mathfp::invalid_arg("coefficient is not finite").ctx("name", name)
                );

            if (x <= 0.0)
                return fail(
                    "coefficient must be positive"
                    , mathfp::invalid_arg("coefficient must be positive").ctx("name", name)
                );
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_nonneg(
            Time v, const char* name
        ) {
            const auto x = v.value();
            if (!is_finite(x))
                return fail(
                    "time is not finite"
                    , mathfp::invalid_arg("time is not finite").ctx("name", name)
                );

            if (x < 0.0)
                return fail(
                    "time must be non-negative"
                    , mathfp::invalid_arg("time must be non-negative").ctx("name", name)
                );
            return mathfp::kUnit;
        }

    }  // namespace detail

    inline mathfp::Expected<SearchImpedance> make_search_impedance(
        Dimless a_journey_time
        , Dimless a_transfers
        , Dimless a_fare
        , Time transfer_penalty
    ) {
        using detail::ensure_nonneg;
        if (auto r = ensure_nonneg(a_journey_time  , "a_journey_time"  ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(a_transfers     , "a_transfers"     ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(a_fare          , "a_fare"          ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(transfer_penalty, "transfer_penalty"); !r) return mathfp::unexpected(r.error());
        return SearchImpedance{ a_journey_time, a_transfers, a_fare, transfer_penalty };
    }

    inline mathfp::Expected<TransferLimits> make_transfer_limits(
        TransferCount max_transfers
        , Time min_transfer_wait
        , Time max_transfer_wait
        , bool allow_start_wait
        , bool allow_end_wait
    ) {
        if (max_transfers.get() < 0)
            return detail::fail(
                "max_transfers must be non-negative"
                , mathfp::invalid_arg("max_transfers must be non-negative")
                    .ctx("max_transfers", max_transfers.get())
            );

        if (auto r = detail::ensure_nonneg(min_transfer_wait, "min_transfer_wait"); !r)
            return mathfp::unexpected(r.error());

        if (auto r = detail::ensure_nonneg(max_transfer_wait, "max_transfer_wait"); !r)
            return mathfp::unexpected(r.error());

        if (min_transfer_wait.value() > max_transfer_wait.value())
            return detail::fail(
                "min_transfer_wait must be <= max_transfer_wait"
                , mathfp::invalid_arg("min_transfer_wait must be <= max_transfer_wait")
                    .ctx("min_transfer_wait", min_transfer_wait.value())
                    .ctx("max_transfer_wait", max_transfer_wait.value())
            );

        return TransferLimits{
            max_transfers, min_transfer_wait, max_transfer_wait, allow_start_wait, allow_end_wait
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
        using detail::ensure_nonneg;
        if (auto r = ensure_nonneg(imp_mult, "imp_mult"); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(imp_add , "imp_add" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_mult , "jt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_add  , "jt_add"  ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_mult , "nt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_add  , "nt_add"  ); !r) return mathfp::unexpected(r.error());
        return SearchTolerances{ imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add };
    }

    inline mathfp::Expected<ChoiceTolerances> make_choice_tolerances(
        Dimless imp_mult
        , Dimless imp_add
        , Dimless jt_mult
        , Dimless jt_add
        , Dimless nt_mult
        , Dimless nt_add
    ) {
        using detail::ensure_nonneg;
        if (auto r = ensure_nonneg(imp_mult, "imp_mult"); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(imp_add , "imp_add" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_mult , "jt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(jt_add  , "jt_add"  ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_mult , "nt_mult" ); !r) return mathfp::unexpected(r.error());
        if (auto r = ensure_nonneg(nt_add  , "nt_add"  ); !r) return mathfp::unexpected(r.error());
        return ChoiceTolerances{ imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add };
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
        using detail::ensure_nonneg;
        using detail::ensure_positive;

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
            q_time, q_departure, q_fare, beta, boxcox_t, gamma, x_scale, y_scale, z_scale
        };
    }

    inline mathfp::Expected<SearchParams> make_search_params(
        SearchImpedance impedance
        , TransferLimits transfers
        , SearchTolerances search_tolerances
        , ChoiceTolerances choice_tolerances
        , SplitParams split
    ) {
        return SearchParams{
            std::move(impedance)
            , std::move(transfers)
            , std::move(search_tolerances)
            , std::move(choice_tolerances)
            , std::move(split)
        };
    }

}  // namespace timetable::domain
