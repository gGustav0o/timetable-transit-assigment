#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params/tolerances.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    inline mathfp::Expected<ToleranceMultiplier> make_tolerance_multiplier(
          Dimless     value
        , const char* name
    ) {
        MATHFP_TRY(validation::ensure_nonneg(value, name));
        return ToleranceMultiplier{ mathfp::units::as_dimless(value) };
    }

    inline mathfp::Expected<ImpedanceTolerance> make_impedance_tolerance(
          Dimless     value
        , const char* name
    ) {
        MATHFP_TRY(validation::ensure_nonneg(value, name));
        return ImpedanceTolerance{ mathfp::units::as_dimless(value) };
    }

    inline mathfp::Expected<JourneyTimeTolerance> make_journey_time_tolerance(
          Time        value
        , const char* name
    ) {
        MATHFP_TRY(validation::ensure_nonneg(value, name));
        return JourneyTimeTolerance{ value };
    }

    inline mathfp::Expected<TransferCountTolerance> make_transfer_count_tolerance(
          Dimless     value
        , const char* name
    ) {
        MATHFP_TRY(validation::ensure_nonneg(value, name));
        return TransferCountTolerance{ mathfp::units::as_dimless(value) };
    }

    namespace params_detail {

        template <class Tolerance>
        inline mathfp::Expected<Tolerance> make_tolerance_bundle(
              Dimless imp_mult
            , Dimless imp_add
            , Dimless jt_mult
            , Time    jt_add
            , Dimless nt_mult
            , Dimless nt_add
        ) {
            MATHFP_TRY_LET(
                  ToleranceMultiplier
                , imp_mult_value
                , make_tolerance_multiplier(imp_mult, "imp_mult")
            );
            MATHFP_TRY_LET(
                  ImpedanceTolerance
                , imp_add_value
                , make_impedance_tolerance(imp_add, "imp_add")
            );
            MATHFP_TRY_LET(
                  ToleranceMultiplier
                , jt_mult_value
                , make_tolerance_multiplier(jt_mult, "jt_mult")
            );
            MATHFP_TRY_LET(
                  JourneyTimeTolerance
                , jt_add_value
                , make_journey_time_tolerance(jt_add, "jt_add")
            );
            MATHFP_TRY_LET(
                  ToleranceMultiplier
                , nt_mult_value
                , make_tolerance_multiplier(nt_mult, "nt_mult")
            );
            MATHFP_TRY_LET(
                  TransferCountTolerance
                , nt_add_value
                , make_transfer_count_tolerance(nt_add, "nt_add")
            );
            return Tolerance{
                  .imp_mult = imp_mult_value
                , .imp_add  = imp_add_value
                , .jt_mult  = jt_mult_value
                , .jt_add   = jt_add_value
                , .nt_mult  = nt_mult_value
                , .nt_add   = nt_add_value
            };
        }

    }  // namespace params_detail

    inline mathfp::Expected<SearchTolerances> make_search_tolerances(
          Dimless imp_mult
        , Dimless imp_add
        , Dimless jt_mult
        , Time    jt_add
        , Dimless nt_mult
        , Dimless nt_add
    ) {
        return params_detail::make_tolerance_bundle<SearchTolerances>(
            imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add
        );
    }

    inline mathfp::Expected<ChoiceTolerances> make_choice_tolerances(
          Dimless imp_mult
        , Dimless imp_add
        , Dimless jt_mult
        , Time    jt_add
        , Dimless nt_mult
        , Dimless nt_add
    ) {
        return params_detail::make_tolerance_bundle<ChoiceTolerances>(
            imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add
        );
    }

}  // namespace timetable::domain
