#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <mathfp/types/strong_type.hpp>

#include "timetable/enum_string.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

    struct TransferCountTag {};

    using TransferCount = mathfp::StrongType<
        std::int32_t
        , TransferCountTag
        , mathfp::strong_detail::EqualityComparable
        , mathfp::strong_detail::Ordered
    >;

    struct FareNormalization final {
        enum class Kind : std::uint8_t {
              None
            , Mean
            , Median
            , P95
            , FixedScale
        };

        Kind kind{ Kind::Median };
        double fixed_scale{ 1.0 };
    };

    /**
     * @brief Component search impedance weights for branch-and-bound search.
     *
     * The search cost is a generalized cost over parameter-independent
     * connection metrics. It keeps time components separated so dominance and
     * choice are evaluated in the same behavioral space as the assignment
     * model, rather than through an already aggregated journey time.
     *
     * IMP(c) =
     *   w_ivt  * IVT(c)
     * + w_acc  * ACC(c)
     * + w_egr  * EGR(c)
     * + w_walk * TWalk(c)
     * + w_twt  * TWait(c)
     * + w_nt   * NT(c)
     * + w_fare * FARE(c).
     *
     * volume_capacity_ratio is parsed and stored for the future
     * capacity-aware search layer. It must not be applied inside the current
     * branch-and-bound search until capacity costs are fixed exogenously for a
     * search iteration.
     */
    struct SearchImpedance final {
        Dimless           in_vehicle_time{};
        Dimless           access_time{};
        Dimless           egress_time{};
        Dimless           transfer_walk_time{};
        Dimless           transfer_wait_time{};
        Dimless           transfer_count{};
        Dimless           fare{};
        Dimless           volume_capacity_ratio{};
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
     * @brief Submodels used by the demand split step.
     *
     * PJT(c) =
     *   w_ivt  * IVT(c)
     * + w_acc  * ACC(c)
     * + w_egr  * EGR(c)
     * + w_walk * TWalk(c)
     * + w_twt  * TWait(c)
     * + w_nt   * NT(c)
     * + w_vcr  * CAP(c, L)
     *
     * CAP(c, L) is a future capacity exposure term of the form
     * sum_e ride_time_e * phi(load_e / capacity_e). The factor is parsed now,
     * but the exposure term is added only by the capacity-aware split layer.
     *
     * U_a(c) = u_early * max(0, start(a) - DEP(c))
     *        + u_late  * max(0, DEP(c) - end(a))
     * IMP_a(c) = q_time * PJT(c) + q_departure * U_a(c) + q_fare * FARE(c).
     *
     * This keeps the split model aligned with the paper while making the two
     * user-defined subfunctions PJT and U_a explicit in the domain model.
     */
    struct PerceivedJourneyTimeWeights final {
        Dimless in_vehicle_time{};
        Dimless access_time{};
        Dimless egress_time{};
        Dimless transfer_walk_time{};
        Dimless transfer_wait_time{};
        Dimless transfer_count{};
        Dimless volume_capacity_ratio{};
    };

    struct TemporalUtilityWeights final {
        Dimless early_departure{};
        Dimless late_departure{};
    };

    enum class SplitChoiceModel : std::uint8_t {
          Kirchhoff
        , Logit
        , Lohse
        , BoxCox
    };

    inline constexpr std::array kSplitChoiceModelTokens{
          timetable::EnumStringEntry<SplitChoiceModel>{
              SplitChoiceModel::Kirchhoff, "Kirchhoff"
          }
        , timetable::EnumStringEntry<SplitChoiceModel>{
              SplitChoiceModel::Logit, "Logit"
          }
        , timetable::EnumStringEntry<SplitChoiceModel>{
              SplitChoiceModel::Lohse, "Lohse"
          }
        , timetable::EnumStringEntry<SplitChoiceModel>{
              SplitChoiceModel::BoxCox, "BoxCox"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SplitChoiceModel value
    ) noexcept {
        return timetable::enum_to_string(value, kSplitChoiceModelTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SplitChoiceModel> split_choice_model_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSplitChoiceModelTokens);
    }

    struct SplitChoiceModelConfig final {
        SplitChoiceModel model{ SplitChoiceModel::BoxCox };
        Dimless          exponent{};
    };

    /**
     * @brief Optional transformation of raw split impedance before choice weights.
     *
     * splitPara.BoxCoxTransformImp controls whether the raw split impedance IMP
     * is transformed before it is passed to the selected choice model:
     * - false: use stabilized raw IMP;
     * - true: use box_cox_transform(IMP, t), where t is stored in boxcox_t.
     *
     * This is intentionally separate from SplitChoiceModelConfig. choiceModel
     * selects the demand-allocation weight family, while this config selects the
     * argument transformation applied to IMP. splitPara.BoxCoxLambda is not
     * modeled here until its relation to BoxCoxPara is clarified.
     */
    struct SplitImpedanceTransformConfig final {
        bool    boxcox_transform_enabled{};
        Dimless boxcox_t{};
    };

    struct SplitIndependenceConfig final {
        bool    enabled{ true };
        Dimless gamma{};
        Dimless temporal_similarity_scale{};
        Dimless higher_quality_scale{};
        Dimless lower_quality_scale{};
    };

    /**
     * @brief Parameters for demand split across connections.
     *
     * choice_model controls the demand-allocation model and its exponent.
     * impedance_transform controls optional transformation of raw split
     * impedance before the model-specific weight is evaluated.
     * independence controls whether the alternative-overlap correction is
     * active and stores the parameters of the evaluation function f_c(c') from
     * the paper:
     * - temporal_similarity_scale corresponds to s_x
     * - higher_quality_scale is used for s_y / s_z when the base connection c
     *   is superior, so it should typically be >= lower_quality_scale
     * - lower_quality_scale is used for s_y / s_z when the base connection c
     *   is inferior
     */
    struct SplitParams final {
        Dimless                     q_time{};
        Dimless                     q_departure{};
        Dimless                     q_fare{};
        PerceivedJourneyTimeWeights perceived_journey_time{};
        TemporalUtilityWeights      temporal_utility{};
        SplitChoiceModelConfig      choice_model{};
        SplitImpedanceTransformConfig impedance_transform{};
        SplitIndependenceConfig     independence{};
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
