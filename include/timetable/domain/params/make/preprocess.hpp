#pragma once

#include <optional>

#include <mathfp/core/expected.hpp>
#include <mathfp/core/try.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/params/preprocess.hpp"
#include "timetable/domain/validation.hpp"

namespace timetable::domain {

    namespace params_detail {

        inline mathfp::Expected<mathfp::Unit> ensure_walk_cost_weights_nonnegative(
            WalkCostWeights walk_cost
        ) {
            MATHFP_TRY(validation::ensure_nonneg(walk_cost.w_time, "walk_cost.w_time"));
            MATHFP_TRY(validation::ensure_nonneg(walk_cost.w_length, "walk_cost.w_length"));
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_weighted_walk_cost_has_positive_weight(
              WalkCostKind    walk_cost_kind
            , WalkCostWeights walk_cost
        ) {
            if (walk_cost_kind != WalkCostKind::Weighted) {
                return mathfp::kUnit;
            }

            const auto wt = mathfp::units::as_dimless(walk_cost.w_time);
            const auto wl = mathfp::units::as_dimless(walk_cost.w_length);
            if (!(wt > 0.0 || wl > 0.0)) {
                const char* message = "weighted walk cost requires at least one positive weight";
                return validation::fail(message, mathfp::invalid_arg(message));
            }
            return mathfp::kUnit;
        }

        inline mathfp::Expected<mathfp::Unit> ensure_optional_line_speed_positive(
            const std::optional<Speed>& line_speed
        ) {
            if (line_speed) {
                MATHFP_TRY(validation::ensure_positive(*line_speed, "line_speed"));
            }
            return mathfp::kUnit;
        }

    }  // namespace params_detail

    inline mathfp::Expected<PreprocessParams> make_preprocess_params(
          WalkCostKind         walk_cost_kind
        , WalkCostWeights      walk_cost
        , std::optional<Speed> line_speed
        , bool                 strict_trips
        , bool                 allow_overnight
        , bool                 overnight_add_24h
        , bool                 strict_stop_times
        , TimeAggregationKind  time_aggregation
        , bool                 deduplicate_walk_segments
        , bool                 stable_ordering
    ) {
        MATHFP_TRY(params_detail::ensure_walk_cost_weights_nonnegative(walk_cost));
        MATHFP_TRY(params_detail::ensure_weighted_walk_cost_has_positive_weight(
            walk_cost_kind, walk_cost
        ));
        MATHFP_TRY(params_detail::ensure_optional_line_speed_positive(line_speed));

        return PreprocessParams{
              .walk_cost_kind            = walk_cost_kind
            , .walk_cost                 = walk_cost
            , .line_speed                = line_speed
            , .strict_trips              = strict_trips
            , .allow_overnight           = allow_overnight
            , .overnight_add_24h         = overnight_add_24h
            , .strict_stop_times         = strict_stop_times
            , .time_aggregation          = time_aggregation
            , .deduplicate_walk_segments = deduplicate_walk_segments
            , .stable_ordering           = stable_ordering
        };
    }

}  // namespace timetable::domain
