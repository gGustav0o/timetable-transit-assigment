#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params/tolerances.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    namespace params_detail {

        inline mathfp::Expected<mathfp::Unit> ensure_nonnegative_tolerance_inputs(
              Dimless imp_mult
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

        template <class Tolerance>
        inline mathfp::Expected<Tolerance> make_tolerance_bundle(
              Dimless imp_mult
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

    }  // namespace params_detail

    inline mathfp::Expected<SearchTolerances> make_search_tolerances(
          Dimless imp_mult
        , Dimless imp_add
        , Dimless jt_mult
        , Dimless jt_add
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
        , Dimless jt_add
        , Dimless nt_mult
        , Dimless nt_add
    ) {
        return params_detail::make_tolerance_bundle<ChoiceTolerances>(
            imp_mult, imp_add, jt_mult, jt_add, nt_mult, nt_add
        );
    }

}  // namespace timetable::domain
