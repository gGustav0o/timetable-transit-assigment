#include "timetable/domain/assignment/skim_config.hpp"

#include <cmath>
#include <utility>

#include <mathfp/core/error.hpp>

namespace timetable::domain::assignment {

    mathfp::Expected<mathfp::Unit> validate_skim_matrix_config(
        const SkimMatrixConfig& config
    ) {
        if (!std::isfinite(config.quantile)
            || config.quantile < 0.0
            || config.quantile > 1.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("skim quantile must be finite and in [0, 1]")
                    .ctx("quantile", config.quantile)
            );
        }

        if (!std::isfinite(config.low_impedance_connection_share)
            || config.low_impedance_connection_share < 0.0
            || config.low_impedance_connection_share > 1.0) {
            return mathfp::unexpected(
                mathfp::invalid_arg("skim low impedance connection share must be finite and in [0, 1]")
                    .ctx("low_impedance_connection_share", config.low_impedance_connection_share)
            );
        }

        return mathfp::kUnit;
    }

    mathfp::Expected<SkimMatrixConfig> make_skim_matrix_config(
          bool                enabled
        , SkimAggregationFunc func
        , bool                volume_weighted
        , double              quantile
        , double              low_impedance_connection_share
    ) {
        SkimMatrixConfig config{
              .enabled                         = enabled
            , .func                            = func
            , .volume_weighted                 = volume_weighted
            , .quantile                        = quantile
            , .low_impedance_connection_share  = low_impedance_connection_share
        };
        auto validated = validate_skim_matrix_config(config);
        if (!validated) {
            return mathfp::unexpected(std::move(validated.error()));
        }
        return config;
    }

}  // namespace timetable::domain::assignment
