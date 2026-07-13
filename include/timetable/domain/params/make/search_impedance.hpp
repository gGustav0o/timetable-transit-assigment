#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params/search_impedance.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    namespace params_detail {

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
            MATHFP_TRY(validation::ensure_nonneg(impedance.volume_capacity_ratio, "search_impedance.volume_capacity_ratio"));
            return mathfp::kUnit;
        }

    }  // namespace params_detail

    inline mathfp::Expected<SearchImpedance> make_search_impedance(
          Dimless           in_vehicle_time
        , Dimless           access_time
        , Dimless           egress_time
        , Dimless           transfer_walk_time
        , Dimless           transfer_wait_time
        , Dimless           transfer_count
        , Dimless           fare
        , FareNormalization fare_normalization = {}
        , Dimless           volume_capacity_ratio = Dimless{ 0.0 }
    ) {
        SearchImpedance impedance{
              .in_vehicle_time    = in_vehicle_time
            , .access_time        = access_time
            , .egress_time        = egress_time
            , .transfer_walk_time = transfer_walk_time
            , .transfer_wait_time = transfer_wait_time
            , .transfer_count     = transfer_count
            , .fare               = fare
            , .volume_capacity_ratio = volume_capacity_ratio
            , .fare_normalization = fare_normalization
        };
        MATHFP_TRY(params_detail::ensure_nonnegative_search_impedance_inputs(impedance));
        return impedance;
    }

}  // namespace timetable::domain
