#pragma once

#include <string>

#include "params_txt.hpp"

namespace timetable::infra::params_txt::detail::draft {

    struct SearchParamsObjects final {
        const Object* search_para{};
        const Object* choice_para{};
        const Object* split_para{};
    };

    struct SearchObjects final {
        const Object* tolerances{};
        const Object* temporal{};
        const Object* impedance{};
    };

    struct ChoiceObjects final {
        const Object* tolerances{};
    };

    struct SplitObjects final {
        const Object* independence{};
        const Object* impedance{};
    };

    struct SplitImpedanceObjects final {
        const Object* perceived_journey_time{};
    };

    struct Tolerance final {
        double imp_mult{};
        double imp_add{};
        double jt_mult{};
        double jt_add{};
        double nt_mult{};
        double nt_add{};
    };

    struct TransferLimits final {
        double max_transfers{};
        double min_transfer_wait{};
        double max_transfer_wait{};
    };

    struct SearchImpedance final {
        double in_vehicle_time{};
        double access_time{};
        double egress_time{};
        double transfer_walk_time{};
        double transfer_wait_time{};
        double transfer_count{};
        double fare{};
        double volume_capacity_ratio{};
    };

    struct SplitChoiceModel final {
        std::string choice_model{};
    };

    struct ChoiceModelExponent final {
        double exponent{};
    };

    struct SplitScalars final {
        double boxcox_t{};
    };

    struct SplitImpedance final {
        double time{};
        double departure_early{};
        double departure_late{};
        double fare{};
    };

    struct PerceivedJourneyTime final {
        double in_vehicle_time{};
        double access_time{};
        double egress_time{};
        double transfer_walk_time{};
        double transfer_wait_time{};
        double transfer_count{};
        double volume_capacity_ratio{};
    };

    struct SplitIndependence final {
        bool   enabled{};
        double gamma{};
        double temporal_similarity_scale{};
        double higher_quality_scale{};
        double lower_quality_scale{};
        double higher_perceived_journey_time_scale{};
        double lower_perceived_journey_time_scale{};
        double higher_fare_scale{};
        double lower_fare_scale{};
    };

    struct SkimMatrix final {
        bool        enabled{};
        std::string func{};
        double      volume_weighted{};
        double      quantile{};
        double      low_impedance_connection_share{};
    };

    struct AssignmentPeriod final {
        double pre_assign_period{};
        double post_assign_period{};
    };

    struct ConnectionDeletion final {
        bool delete_outside_assignment_period{};
        bool delete_departures_before_assignment_period_for_departure_based{};
        bool delete_arrivals_after_assignment_period_for_arrival_based{};
    };

    struct DemandSegmentTime final {
        bool dep_based_demand_segment{};
        bool consider_connections_with_positive_delta_t{};
    };

    struct SearchParams final {
        Tolerance           search_tolerances{};
        Tolerance           choice_tolerances{};
        TransferLimits      transfers{};
        SearchImpedance     impedance{};
        SplitImpedance      split_impedance{};
        PerceivedJourneyTime split_perceived_journey_time{};
        SplitIndependence   split_independence{};
        SplitChoiceModel    split_choice_model{};
        ChoiceModelExponent split_choice_model_exponent{};
        SplitScalars        split_scalars{};
        bool                split_boxcox_transform_enabled{};
    };

}  // namespace timetable::infra::params_txt::detail::draft
