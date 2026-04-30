#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/enum_string.hpp"

namespace timetable::domain::assignment {

    /**
     * @brief Aggregation rule for OD-interval skim rows.
     *
     * The enum describes the mathematical aggregation requested by params.txt;
     * the actual skim calculation is implemented in the skim module.
     */
    enum class SkimAggregationFunc : std::uint8_t {
          Mean
        , Quantile
    };

    inline constexpr std::array kSkimAggregationFuncTokens{
          timetable::EnumStringEntry<SkimAggregationFunc>{
              SkimAggregationFunc::Mean, "Mean"
          }
        , timetable::EnumStringEntry<SkimAggregationFunc>{
              SkimAggregationFunc::Mean, "mean"
          }
        , timetable::EnumStringEntry<SkimAggregationFunc>{
              SkimAggregationFunc::Quantile, "Quantile"
          }
        , timetable::EnumStringEntry<SkimAggregationFunc>{
              SkimAggregationFunc::Quantile, "quantile"
          }
    };

    [[nodiscard]] inline constexpr std::string_view to_string(
        SkimAggregationFunc value
    ) noexcept {
        return timetable::enum_to_string(value, kSkimAggregationFuncTokens);
    }

    [[nodiscard]] inline constexpr std::optional<SkimAggregationFunc> skim_aggregation_func_from_string(
        std::string_view token
    ) noexcept {
        return timetable::enum_from_string(token, kSkimAggregationFuncTokens);
    }

    /**
     * @brief Runtime request for skim-matrix calculation.
     *
     * enabled corresponds to basePara.calcSkimMatr. The remaining fields mirror
     * skimMatrixPara and intentionally live outside SearchParams, because skim
     * matrices are an analytical projection over assignment results rather than
     * a search or split model parameter.
     *
     * low_impedance_connection_share is the fraction of chosen alternatives
     * retained after sorting by increasing split impedance. It limits the
     * connection support before aggregation; volume_weighted controls only the
     * aggregation weights inside the retained support.
     */
    struct SkimMatrixConfig final {
        bool                enabled{ false };
        SkimAggregationFunc func{ SkimAggregationFunc::Mean };
        bool                volume_weighted{ true };
        double              quantile{ 0.5 };
        double              low_impedance_connection_share{ 1.0 };
    };

    mathfp::Expected<mathfp::Unit> validate_skim_matrix_config(
        const SkimMatrixConfig& config
    );

    mathfp::Expected<SkimMatrixConfig> make_skim_matrix_config(
          bool                enabled
        , SkimAggregationFunc func
        , bool                volume_weighted
        , double              quantile
        , double              low_impedance_connection_share
    );

}  // namespace timetable::domain::assignment
