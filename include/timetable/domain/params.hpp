#pragma once

#include <cstdint>
#include <optional>
#include <mathfp/types/units.hpp>
#include <mathfp/types/strong_type.hpp>

namespace timetable::domain {

    // ”же было в model???
    using Time    = mathfp::units::Quantity<double, mathfp::units::Time>;
    using Dimless = mathfp::units::Quantity<double, mathfp::units::Dimless>;
    using Speed   = mathfp::units::Quantity<double, mathfp::units::Dim<1, 0, -1, 0, 0, 0, 0>>;

    struct TransferCountTag {};

    using TransferCount = mathfp::StrongType<
        std::int32_t
        , TransferCountTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    /**
     * @brief Search impedance weights for branch-and-bound connection search.
     *
     * IMP(c) = a_journey_time * JT(c) + a_transfers * NT(c) + a_fare * FARE(c).
     * transfer_penalty is a fixed additive time penalty per transfer used during search.
     */
    struct FareNormalization final {
        enum class Kind : std::uint8_t {
            None
            , Mean
            , Median
            , P95
            , FixedScale
        };

        Kind   kind{ Kind::Median };
        double fixed_scale{ 1.0 };
    };

    struct SearchImpedance final {
        Dimless a_journey_time{};
        Dimless a_transfers{};
        Dimless a_fare{};
        Time    transfer_penalty{};
        FareNormalization fare_normalization{};
    };

    /**
     * @brief Hard constraints for transfer feasibility during search.
     */
    struct TransferLimits final {
        TransferCount max_transfers{};
        Time          min_transfer_wait{};
        Time          max_transfer_wait{};
        bool          allow_start_wait{};
        bool          allow_end_wait{};
    };

    /**
     * @brief Tolerances applied during the search at intermediate nodes.
     *
     * Used to prune dominated connections early while keeping diversity.
     *
     * Conditions use min values among known connections to a node:
     *  - IMP(c*) <= imp_mult * min IMP(c) + imp_add
     *  - JT(c*)  <= jt_mult  * min JT(c)  + jt_add
     *  - NT(c*)  <= nt_mult  * min NT(c)  + nt_add
     */
    struct SearchTolerances final {
        Dimless imp_mult{};
        Dimless imp_add{};
        Dimless jt_mult{};
        Dimless jt_add{};
        Dimless nt_mult{};
        Dimless nt_add{};
    };

    /**
     * @brief Tolerances applied at the final connection choice step.
     *
     * Typically stricter than SearchTolerances.
     *
     * Conditions use min values among all connections to the destination:
     *  - IMP(c) <= imp_mult * min IMP + imp_add
     *  - JT(c)  <= jt_mult  * min JT  + jt_add
     *  - NT(c)  <= nt_mult  * min NT  + nt_add
     */
    struct ChoiceTolerances final {
        Dimless imp_mult{};
        Dimless imp_add{};
        Dimless jt_mult{};
        Dimless jt_add{};
        Dimless nt_mult{};
        Dimless nt_add{};
    };

    /**
     * @brief Parameters for demand split across connections.
     *
     * IMP_a(c) = q_time * PJT(c) + q_departure * U_a(c) + q_fare * FARE(c).
     * beta controls MNL sensitivity; boxcox_t is the Box-Cox parameter.
     * gamma and scales control independence/similarity effects.
     */
    struct SplitParams final {
        Dimless q_time{};
        Dimless q_departure{};
        Dimless q_fare{};
        Dimless beta{};
        Dimless boxcox_t{};
        Dimless gamma{};
        Dimless x_scale{};
        Dimless y_scale{};
        Dimless z_scale{};
    };

    enum class WalkCostKind : std::uint8_t {
        Time
        , Length
        , Weighted
    };

    struct WalkCostWeights final {
        Dimless w_time{};
        Dimless w_length{};
    };

    enum class TimeAggregationKind : std::uint8_t {
        Mean
        , Median
        , Minimum
    };

    struct PreprocessParams final {
        WalkCostKind         walk_cost_kind            { WalkCostKind::Time };
        WalkCostWeights      walk_cost                 {};
        std::optional<Speed> line_speed                {};
        bool                 strict_trips              { true };
        bool                 allow_overnight           { false };
        bool                 overnight_add_24h         { true };
        bool                 strict_stop_times         { true };
        TimeAggregationKind  time_aggregation          { TimeAggregationKind::Mean };
        bool                 deduplicate_walk_segments { true };
        bool                 stable_ordering           { true };
    };

    /**
     * @brief Full parameter bundle for timetable-based assignment.
     */
    struct SearchParams final {
        PreprocessParams preprocess{};
        SearchImpedance  impedance{};
        TransferLimits   transfers{};
        SearchTolerances search_tolerances{};
        ChoiceTolerances choice_tolerances{};
        SplitParams      split{};
    };

}  // namespace timetable::domain
