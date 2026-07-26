#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "timetable/enum_string.hpp"
#include "timetable/domain/scalars.hpp"

namespace timetable::domain {

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
     * This keeps the split model aligned with the split formulation while making the two
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
     * This is intentionally separate from SplitChoiceModelConfig. choiceModel
     * selects the demand-allocation weight family, while this config selects the
     * argument transformation applied to IMP.
     */
    //tex:
    // For the Box--Cox MNL form, enable this transform and use
    // `SplitChoiceModel::BoxCox`:
    // $$b^{(t)}(IMP)=\begin{cases}(IMP^t-1)/t,&t\ne0,\\ \log(IMP),&t=0.\end{cases}$$
    // The transform is separate from the weight family so experiments can use
    // the same normalized choice model with raw or transformed impedance.
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
        Dimless higher_perceived_journey_time_scale{};
        Dimless lower_perceived_journey_time_scale{};
        Dimless higher_fare_scale{};
        Dimless lower_fare_scale{};
    };

    //tex:
    // Independence parameters implement the split overlap correction
    // $$IND(c)=\frac{1}{1+\sum_{c'\ne c}f_c(c')}.$$
    // `temporal_similarity_scale` is $$s_x$$. The perceived-journey-time
    // scales provide $$s_y$$ and the fare scales provide $$s_z$$; each pair is
    // selected asymmetrically according to whether compared connection $$c'$$
    // is superior or inferior to base connection $$c$$ in the corresponding
    // attribute.

    /**
     * @brief Parameters for demand split across connections.
     *
     * choice_model controls the demand-allocation model and its exponent.
     * impedance_transform controls optional transformation of raw split
     * impedance before the model-specific weight is evaluated.
     * independence controls whether the alternative-overlap correction is
     * active and stores the parameters of the split evaluation function
     * f_c(c').
     * - temporal_similarity_scale corresponds to s_x
     * - higher/lower_perceived_journey_time_scale correspond to sign-dependent
     *   s_y. "higher" means that compared connection c' has lower PJT than
     *   base connection c.
     * - higher/lower_fare_scale correspond to sign-dependent s_z
     *   by the same rule for fare.
     * - higher/lower_quality_scale are older-input fallbacks for older params
     *   files and should mirror the source-article-specific scales when both are absent
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

}  // namespace timetable::domain
