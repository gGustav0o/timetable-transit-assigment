#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include "timetable/domain/assignment/split/choice_weight.hpp"

namespace timetable::domain::assignment {
namespace {

    [[nodiscard]] SplitChoiceModelConfig choice_model(
          SplitChoiceModel model
        , double           exponent
    ) {
        return SplitChoiceModelConfig{
              .model = model
            , .exponent = Dimless{ exponent }
        };
    }

}  // namespace

TEST(SplitChoiceWeight, BoxCoxUsesTransformedImpedanceLogWeight) {
    const auto result = split_choice_log_weight(
          choice_model(SplitChoiceModel::BoxCox, 2.0)
        , SplitTransformedImpedance{ 3.0 }
        , SplitIndependenceWeight{ 0.5 }
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_NEAR(result->get(), std::log(0.5) - 2.0 * 3.0, 1e-15);
}

TEST(SplitChoiceWeight, LogitUsesLinearImpedanceLogWeight) {
    const auto result = split_choice_log_weight(
          choice_model(SplitChoiceModel::Logit, 1.5)
        , SplitTransformedImpedance{ 4.0 }
        , SplitIndependenceWeight{ 0.25 }
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_NEAR(result->get(), std::log(0.25) - 1.5 * 4.0, 1e-15);
}

TEST(SplitChoiceWeight, KirchhoffUsesLogImpedanceWeight) {
    const auto result = split_choice_log_weight(
          choice_model(SplitChoiceModel::Kirchhoff, 2.0)
        , SplitTransformedImpedance{ 9.0 }
        , SplitIndependenceWeight{ 0.75 }
    );

    ASSERT_TRUE(result.has_value()) << result.error().to_string();
    EXPECT_NEAR(result->get(), std::log(0.75) - 2.0 * std::log(9.0), 1e-15);
}

TEST(SplitChoiceWeight, RejectsUnsupportedAndInvalidInputs) {
    EXPECT_FALSE(validate_supported_split_choice_model(SplitChoiceModel::Lohse).has_value());
    EXPECT_FALSE(split_choice_log_weight(
          choice_model(SplitChoiceModel::Lohse, 1.0)
        , SplitTransformedImpedance{ 1.0 }
        , SplitIndependenceWeight{ 1.0 }
    ).has_value());
    EXPECT_FALSE(split_choice_log_weight(
          choice_model(SplitChoiceModel::BoxCox, 1.0)
        , SplitTransformedImpedance{ std::numeric_limits<double>::infinity() }
        , SplitIndependenceWeight{ 1.0 }
    ).has_value());
    EXPECT_FALSE(split_choice_log_weight(
          choice_model(SplitChoiceModel::BoxCox, 1.0)
        , SplitTransformedImpedance{ 1.0 }
        , SplitIndependenceWeight{ 0.0 }
    ).has_value());
    EXPECT_FALSE(split_choice_log_weight(
          choice_model(SplitChoiceModel::BoxCox, std::numeric_limits<double>::quiet_NaN())
        , SplitTransformedImpedance{ 1.0 }
        , SplitIndependenceWeight{ 1.0 }
    ).has_value());
}

}  // namespace timetable::domain::assignment
