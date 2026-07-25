#pragma once

#include <mathfp/core/expected.hpp>
#include <mathfp/core/unit.hpp>

#include "timetable/domain/assignment/split/types.hpp"
#include "timetable/domain/params/split.hpp"

namespace timetable::domain::assignment {

    [[nodiscard]] mathfp::Expected<mathfp::Unit> validate_supported_split_choice_model(
        SplitChoiceModel model
    );

    [[nodiscard]] mathfp::Expected<SplitLogWeight> split_choice_log_weight(
          const SplitChoiceModelConfig& model
        , SplitTransformedImpedance      transformed_impedance
        , SplitIndependenceWeight       independence
    );

}  // namespace timetable::domain::assignment
